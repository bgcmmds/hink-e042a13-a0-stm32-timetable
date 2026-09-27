#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
server.py —— 课程表编辑器的本地服务

职责：给网页提供 HTTP 接口，代替人跑命令。
  - 读写 tools/timetable_data.txt        （课表数据）
  - 生成 Core/Src/my_courses.c           （编译进固件）
  - 扫描新汉字并重新生成字库              （避免屏上出现空心方框）
  - 可选：调 cmake 编译 / openocd 烧录    （需自行配置，见下）

启动：
    python tools/server.py
    （或双击 tools/课表编辑器.bat 自动完成启动 + 开浏览器）

为什么需要本地服务：浏览器出于安全限制，不能直接读写本地文件、
也不能调用编译器。所以由这个进程代劳，网页只负责界面。

★ 通用性说明
  编辑课表 + 生成代码 + 生成字库 这几步**开箱即用**，只需 Python 3。
  编译 / 烧录需要本机装了 cmake / openocd，且命令可能因人而异，
  所以在网页「设置」里配置，本文件不写死任何路径。
"""

import http.server
import json
import os
import re
import shutil
import socketserver
import subprocess
import sys
import threading
import webbrowser

# ── 路径（都以项目根为基准）────────────────────────────────────────────────
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)                      # 仓库根
DATA_FILE  = os.path.join(HERE, "timetable_data.txt")
CHARS_FILE = os.path.join(HERE, "chars.txt")
OUT_C_FILE = os.path.join(ROOT, "Core", "Src", "my_courses.c")
WEB_DIR    = os.path.join(HERE, "web")
FONT_PY    = os.path.join(HERE, "font_export.py")

PORT = 8000

# ── 可选工具链配置（都可在网页「设置」里改，不写死本机路径）──────────────
CONFIG_FILE = os.path.join(HERE, "editor_config.json")

def _default_font():
    """挑一个系统里存在的 CJK 字体作为默认值。
    找不到就留空，让用户在设置里自己填 —— 不假设任何人的系统。"""
    cands = [
        # Windows
        "C:/Windows/Fonts/Deng.ttf",       # 等线（细，小字清楚）
        "C:/Windows/Fonts/msyh.ttc",       # 微软雅黑
        "C:/Windows/Fonts/simhei.ttf",     # 黑体
        # macOS
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        # Linux（常见发行版）
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
    ]
    for p in cands:
        if os.path.exists(p):
            return p
    return ""

DEFAULT_CONFIG = {
    "python":  sys.executable,        # 跑 font_export.py 的解释器
    "font":    _default_font(),       # 生成字库用的 TTF/TTC
    "build":   "",                    # 编译命令（留空 = 网页不提供编译按钮）
    "flash":   "",                    # 烧录命令
}

DAY_NAME    = ["周一", "周二", "周三", "周四", "周五"]
PERIOD_NAME = ["上午一", "上午二", "下午一", "下午二", "晚上"]

# 编译/烧录时若某个命令一直不出结果，防止网页无限等待
CMD_TIMEOUT = 300


# ══════════════════════════════════════════════════════════════════════════
# 配置
# ══════════════════════════════════════════════════════════════════════════

def load_config():
    cfg = dict(DEFAULT_CONFIG)
    try:
        with open(CONFIG_FILE, encoding="utf-8") as f:
            cfg.update(json.load(f))
    except (OSError, ValueError):
        pass
    return cfg


def save_config(cfg):
    with open(CONFIG_FILE, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)


# ══════════════════════════════════════════════════════════════════════════
# 课表数据：读写 timetable_data.txt
# ══════════════════════════════════════════════════════════════════════════

def blank_table():
    return {"week": 1, "courses": []}


def load_table():
    """读数据文件。坏行直接跳过，不让一条脏数据毁掉整个课表。"""
    t = blank_table()
    if not os.path.exists(DATA_FILE):
        return t

    with open(DATA_FILE, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("week|"):
                try:
                    t["week"] = max(1, min(99, int(line[5:])))
                except ValueError:
                    pass
            elif line.startswith("course|"):
                parts = line.split("|")
                if len(parts) < 6:
                    continue
                try:
                    day    = int(parts[1])
                    period = int(parts[2])
                    span   = max(1, int(parts[3]))
                except ValueError:
                    continue
                if not (0 <= day <= 4 and 0 <= period <= 4):
                    continue
                if period + span > 5:
                    continue
                t["courses"].append({
                    "day": day, "period": period, "span": span,
                    "name": parts[4], "place": parts[5],
                })
    return t


def save_table(t):
    with open(DATA_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write("# 课程表数据 —— 由编辑器生成，可手动编辑\n")
        f.write("# 格式: week|周次    course|星期|起始节|持续节数|课程名|地点\n")
        f.write("# 星期 0-4 = 周一-周五; 起始节 0-4; 持续节数 >=1\n")
        f.write("week|%d\n" % t["week"])
        for c in t["courses"]:
            f.write("course|%d|%d|%d|%s|%s\n"
                    % (c["day"], c["period"], c["span"], c["name"], c["place"]))


# ══════════════════════════════════════════════════════════════════════════
# 校验（前端也会查一遍，这里是后端的兜底）
# ══════════════════════════════════════════════════════════════════════════

def conflicts(courses):
    """返回冲突的课程下标集合。两门课同一天且节次区间相交即冲突。"""
    bad = set()
    for i in range(len(courses)):
        a = courses[i]
        for j in range(i + 1, len(courses)):
            b = courses[j]
            if a["day"] != b["day"]:
                continue
            if b["period"] < a["period"] + a["span"] and \
               a["period"] < b["period"] + b["span"]:
                bad.add(i)
                bad.add(j)
    return bad


def validate(t):
    """返回错误列表（空 = 通过）。"""
    errs = []
    if not (1 <= t["week"] <= 99):
        errs.append("周次必须在 1~99")
    for i, c in enumerate(t["courses"]):
        tag = "第%d门「%s」" % (i + 1, c["name"] or "无名")
        if not c["name"].strip():
            errs.append("%s：课程名不能为空" % tag)
        if not (0 <= c["day"] <= 4):
            errs.append("%s：星期超范围" % tag)
        if not (0 <= c["period"] <= 4):
            errs.append("%s：起始节超范围" % tag)
        if c["span"] < 1 or c["period"] + c["span"] > 5:
            errs.append("%s：持续节数越界（超出最后一节）" % tag)
    bad = conflicts(t["courses"])
    if bad:
        names = "、".join("第%d门「%s」" % (i + 1, t["courses"][i]["name"])
                          for i in sorted(bad))
        errs.append("时间冲突：%s" % names)
    return errs


# ══════════════════════════════════════════════════════════════════════════
# 生成 my_courses.c
# ══════════════════════════════════════════════════════════════════════════

C_HEADER = """/*
 * my_courses.c —— 课程表数据
 *
 * ★ 本文件由 tools/server.py（网页编辑器）自动生成，不要手改 ★
 *   要改课表请运行：  tools/课表编辑器.bat
 *   或直接编辑：      tools/timetable_data.txt  然后重新生成
 */
#include "my_courses.h"
#include "timetable.h"

void MyCourses_Load(void)
{
    TT_SetWeek(%d);

"""


def generate_c(t):
    out = [C_HEADER % t["week"]]
    courses = t["courses"]

    if not courses:
        out.append("    /* 课表为空。用编辑器添加课程。 */\n")
    else:
        last_day = None
        for c in courses:
            if c["day"] != last_day:
                out.append("\n    /* ── %s ─────────────────────────── */\n"
                           % DAY_NAME[c["day"]])
                last_day = c["day"]

            # 行末注释：人眼看懂（连堂会写成「上午一~上午二 连堂」）
            if c["span"] > 1:
                end = PERIOD_NAME[c["period"] + c["span"] - 1]
                note = "%s %s~%s 连堂" % (DAY_NAME[c["day"]],
                                          PERIOD_NAME[c["period"]], end)
            else:
                note = "%s %s" % (DAY_NAME[c["day"]], PERIOD_NAME[c["period"]])

            out.append('    TT_AddCourse(%d, %d, %d, "%s", "%s");  /* %s */\n'
                       % (c["day"], c["period"], c["span"],
                          c["name"], c["place"], note))

    out.append("}\n")
    text = "".join(out)

    with open(OUT_C_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


# ══════════════════════════════════════════════════════════════════════════
# 字库联动：扫出新汉字 → 追加 chars.txt → 重新生成 font_cn.c
# ══════════════════════════════════════════════════════════════════════════

def is_cjk(ch):
    """只收集汉字（含扩展A）。ASCII 和标点走别的字库或原样显示。"""
    o = ord(ch)
    return (0x4E00 <= o <= 0x9FFF) or (0x3400 <= o <= 0x4DBF)


def missing_chars(t):
    """返回课表里用到、但 chars.txt 中还没有的汉字（去重、有序）。"""
    have = set()
    if os.path.exists(CHARS_FILE):
        with open(CHARS_FILE, encoding="utf-8") as f:
            have = {ch for ch in f.read() if is_cjk(ch)}

    need = set()
    for c in t["courses"]:
        for ch in c["name"] + c["place"]:
            if is_cjk(ch):
                need.add(ch)

    return sorted(need - have, key=ord)


def append_chars(chars):
    with open(CHARS_FILE, "a", encoding="utf-8", newline="\n") as f:
        f.write("\n" + "".join(chars) + "\n")


def font_char_count():
    """读 font_cn.c 里实际有多少字（用于界面显示）。"""
    try:
        with open(os.path.join(ROOT, "Core", "Src", "font_cn.c"),
                  encoding="utf-8") as f:
            return len(re.findall(r"/\* . \(U\+", f.read()))
    except OSError:
        return 0


# ══════════════════════════════════════════════════════════════════════════
# 外部命令
# ══════════════════════════════════════════════════════════════════════════

def run_cmd(args, cwd=ROOT, shell=False):
    """跑一条命令，返回 (ok, 输出文本)。输出用于回显到网页日志。

    shell=False 时 args 是参数列表（本文件内部调用，路径已由我们拼好，安全）；
    shell=True  时 args 是用户自己配的整条命令字符串，交给系统 shell 解析。
    """
    try:
        p = subprocess.run(args, cwd=cwd, shell=shell, capture_output=True,
                           timeout=CMD_TIMEOUT)
    except FileNotFoundError:
        name = args if shell else args[0]
        return False, "找不到命令：%s\n（检查是否已安装并加入 PATH）" % name
    except subprocess.TimeoutExpired:
        return False, "命令超时（%ds）" % CMD_TIMEOUT

    out = ""
    for b in (p.stdout, p.stderr):
        if b:
            out += b.decode("utf-8", errors="replace")
    return p.returncode == 0, out


def do_regen_font(cfg, log):
    """字库有缺字时重新生成。返回 (ok, 新字数)。"""
    # 注意：不传 --threshold，用 font_export.py 里的默认值（当前 100）。
    # 该值是「清晰度」的关键：等线 16px 用 128 会丢笔画（屏上缺笔）。
    cmd = [cfg["python"], FONT_PY, cfg["font"], CHARS_FILE,
           os.path.join(ROOT, "Core", "Src", "font_cn.c"), "FontCN",
           "--size", "16"]
    log("生成字库：%s" % " ".join(cmd))
    ok, out = run_cmd(cmd)
    log(out.rstrip())
    if not ok:
        return False, 0
    return True, font_char_count()


def _run_user_cmd(template, log, label):
    """跑用户在「设置」里配的命令。留空则跳过（返回 None = 未配置）。

    命令里可用占位符，运行时替换：
        {root}  仓库根目录
        {port}  本机串口（仅作占位，实际由用户命令自己决定）
    """
    tpl = (template or "").strip()
    if not tpl:
        return None                      # 未配置：调用方跳过这一步

    cmd = tpl.replace("{root}", ROOT)
    log("%s：%s" % (label, cmd))
    ok, out = run_cmd(cmd, shell=True)
    log(out.rstrip())
    return ok


def do_build(cfg, log):
    """编译。未配置命令时视为「跳过」，不算失败。"""
    r = _run_user_cmd(cfg.get("build"), log, "编译")
    if r is None:
        log("（未配置编译命令，跳过。可在网页「设置」里填）")
        return True
    if not r:
        log("→ 提示：先确认已执行过 cmake 的 configure 步骤（见 README）")
    return r


def do_flash(cfg, log):
    """烧录。未配置命令时视为「跳过」，不算失败。"""
    r = _run_user_cmd(cfg.get("flash"), log, "烧录")
    if r is None:
        log("（未配置烧录命令，跳过。可在网页「设置」里填）")
        return True
    if not r:
        log("→ 提示：检查调试器是否插好、驱动是否正常")
    return r


# ══════════════════════════════════════════════════════════════════════════
# 工具链自检（让网页能提前告诉用户「哪一步会失败」）
# ══════════════════════════════════════════════════════════════════════════

def tool_status(cfg):
    """检查各项依赖。只报事实，不假设用户环境。"""
    checks = {}
    checks["python"]  = os.path.exists(cfg.get("python", ""))
    checks["font"]    = os.path.exists(cfg.get("font", ""))
    checks["font_py"] = os.path.exists(FONT_PY)
    checks["src"]     = os.path.isdir(os.path.join(ROOT, "Core", "Src"))
    # 编译/烧录是「可选」的：配了才算一项检查
    checks["build_set"] = bool((cfg.get("build") or "").strip())
    checks["flash_set"] = bool((cfg.get("flash") or "").strip())
    # 顺便探测常见工具是否在 PATH（仅作提示，不强制）
    checks["cmake"]   = shutil.which("cmake") is not None
    checks["openocd"] = shutil.which("openocd") is not None
    return checks


# ══════════════════════════════════════════════════════════════════════════
# HTTP 处理
# ══════════════════════════════════════════════════════════════════════════

class Handler(http.server.SimpleHTTPRequestHandler):
    # SimpleHTTPRequestHandler 默认从 cwd 提供服务，这里改到 web/
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=WEB_DIR, **kw)

    # 网页里不需要看到每条静态请求的日志，只保留 API 的
    def log_message(self, fmt, *args):
        if self.path.startswith("/api/"):
            sys.stderr.write("  [api] %s\n" % (fmt % args))

    def _json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n == 0:
            return {}
        try:
            return json.loads(self.rfile.read(n).decode("utf-8"))
        except ValueError:
            return {}

    # ── GET ───────────────────────────────────────────────────────────────
    def do_GET(self):
        if self.path == "/api/state":
            t = load_table()
            cfg = load_config()
            return self._json({
                "table": t,
                "dayNames": DAY_NAME,
                "periodNames": PERIOD_NAME,
                "conflicts": sorted(conflicts(t["courses"])),
                "missingChars": missing_chars(t),
                "fontChars": font_char_count(),
                "tools": tool_status(cfg),
                "config": cfg,
                "root": ROOT,
            })
        if self.path == "/api/config":
            return self._json(load_config())
        return super().do_GET()

    # ── POST ──────────────────────────────────────────────────────────────
    def do_POST(self):
        body = self._body()
        route = self.path

        if route == "/api/save":
            t = body.get("table") or blank_table()
            errs = validate(t)
            if errs:
                return self._json({"ok": False, "errors": errs}, 400)
            save_table(t)
            generate_c(t)
            return self._json({
                "ok": True,
                "message": "已保存数据并生成 my_courses.c",
                "missingChars": missing_chars(t),
                "fontChars": font_char_count(),
            })

        if route == "/api/config":
            cfg = load_config()
            cfg.update({k: v for k, v in body.items() if k in DEFAULT_CONFIG})
            save_config(cfg)
            return self._json({"ok": True, "config": cfg,
                               "tools": tool_status(cfg)})

        if route == "/api/deploy":
            t = body.get("table") or blank_table()
            errs = validate(t)
            if errs:
                return self._json({"ok": False, "errors": errs}, 400)

            cfg = load_config()
            logbuf = []

            def log(s):
                logbuf.append(s)

            # ── ① 存数据 + 生成代码 ──
            save_table(t)
            generate_c(t)
            log("① 已保存数据并生成 %s" % os.path.relpath(OUT_C_FILE, ROOT))

            # ── ② 字库联动 ──
            miss = missing_chars(t)
            if miss:
                log("② 发现 %d 个新汉字：%s" % (len(miss), "".join(miss)))
                append_chars(miss)
                ok, _n = do_regen_font(cfg, log)
                if not ok:
                    return self._json({"ok": False, "stage": "font",
                                       "log": "\n".join(logbuf),
                                       "message": "字库生成失败"}, 500)
            else:
                log("② 字库已包含所有用字，跳过")

            # ── ③ 编译 ──
            log("③ 编译中...")
            if not do_build(cfg, log):
                return self._json({"ok": False, "stage": "build",
                                   "log": "\n".join(logbuf),
                                   "message": "编译失败"}, 500)

            # ── ④ 烧录 ──
            log("④ 烧录中...")
            if not do_flash(cfg, log):
                return self._json({"ok": False, "stage": "flash",
                                   "log": "\n".join(logbuf),
                                   "message": "烧录失败"}, 500)

            log("✓ 全部完成，屏幕将在约 2 秒后刷新")
            return self._json({
                "ok": True,
                "log": "\n".join(logbuf),
                "message": "已烧录完成",
                "fontChars": font_char_count(),
            })

        if route == "/api/check-only":
            # 只做「字库 + 校验」检查，不编译不烧录（界面上的预检查）
            t = body.get("table") or blank_table()
            errs = validate(t)
            miss = missing_chars(t)
            return self._json({"ok": not errs, "errors": errs,
                               "missingChars": miss,
                               "needFontRegen": bool(miss)})

        return self._json({"ok": False, "message": "未知接口"}, 404)


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    cfg = load_config()
    os.makedirs(WEB_DIR, exist_ok=True)

    url = "http://127.0.0.1:%d/" % PORT
    print()
    print("  ╔══════════════════════════════════════════╗")
    print("  ║        课程表编辑器 —— 本地服务           ║")
    print("  ╚══════════════════════════════════════════╝")
    print("  项目根：%s" % ROOT)
    print("  已就绪：%s" % url)
    print("  关掉这个窗口即停止服务。")
    print()

    # 自检，有问题提前说，别等用户点了烧录才发现
    st = tool_status(cfg)
    for name, label in [("cmake", "cmake"), ("openocd", "openocd"),
                        ("python", "Python 解释器"), ("font", "字体文件")]:
        if not st.get(name):
            print("  ! 未找到 %s —— 相应步骤会失败（可在网页设置里改）" % label)
    if st["cmake"] and not st["build"]:
        print("  ! 还没配置过构建目录，请先在项目根执行：cmake --preset Debug")
    print()

    threading.Timer(0.8, lambda: webbrowser.open(url)).start()

    with Server(("127.0.0.1", PORT), Handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n  已停止。")


if __name__ == "__main__":
    main()
