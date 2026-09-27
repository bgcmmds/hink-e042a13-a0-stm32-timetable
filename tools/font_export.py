#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
font_export.py —— 从 TTF 字体生成 font_cn.c（16x16 中文/ASCII 点阵字库）

用法：
    python font_export.py <字体文件.ttf> <字符集文件.txt> [输出文件.c] [字库名]

例：
    # Windows（等线笔画细，16px 小字最清楚）
    python font_export.py C:/Windows/Fonts/Deng.ttf chars.txt Core/Src/font_cn.c FontCN
    # macOS / Linux：换成系统里的中文字体即可
    python font_export.py /System/Library/Fonts/PingFang.ttc chars.txt Core/Src/font_cn.c FontCN

字符集文件：纯文本，把所有需要的汉字写进去即可（换行/空格/标点都会忽略，
重复字自动去重）。默认只收录里面出现的字，好处是体积小（子集字库）。

生成的点阵格式与 font.c 完全一致（务必不要改）：
    每字符 32 字节 = 上半 8 行(16 字节) + 下半 8 行(16 字节)
    每字节 = 一列中的 8 个垂直像素，bit0 在最上、bit7 在最下
    （纵向取模、低位在上）

依赖：freetype-py、Pillow
    pip install freetype-py Pillow
"""

import sys
import os
import argparse

try:
    import freetype
except ImportError:
    sys.exit("缺少 freetype-py，请先: pip install freetype-py")

# ── 点阵尺寸（由命令行 --size 决定，默认 16，与 font.c 的 ASCII 同高）───────
CN_W = 16          # 汉字宽（像素）
CN_H = 16          # 汉字高（像素）
CN_BYTES = (CN_W // 8) * CN_H   # 32 字节/字（16px）；size=20 时为 60 字节
# 灰度二值化阈值。★ 这是清晰度的关键旋钮：
#   等线/雅黑这类细字体在 16px 下，笔画的灰度峰值常低于 128，
#   用 128 会把整个笔画丢掉 → 屏上「缺笔」。实测取 100 能救回大部分笔画。
#   调低 = 笔画更全但更粗；调高 = 更细但会断。100 是等线 16px 的实测最佳点。
BIN_THRESHOLD = 100


def set_size(size):
    """设置点阵边长。必须是 8 的倍数，否则打包会错位。"""
    global CN_W, CN_H, CN_BYTES
    if size % 8 != 0:
        sys.exit("字号必须是 8 的倍数（8/16/24/32），否则纵向取模会错位")
    CN_W = CN_H = size
    CN_BYTES = (CN_W // 8) * CN_H


def collect_chars(path):
    """读字符集文件，去重并保持稳定顺序（按码点排序，便于 diff）。

    支持注释：以 '#' 开头（或行内 # 之后）的内容会被忽略，
    方便在清单里写说明文字，不会把说明当字模生成。
    """
    chars = set()
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.split('#', 1)[0]          # 去掉注释
            chars.update(c for c in line if not c.isspace())
    return sorted(chars, key=ord)


def render_glyph(face, ch):
    """把一个字符渲染成 CN_W x CN_H 的 0/1 点阵（行优先，1=黑）。

    返回 None 表示该字体里没有这个字（缺字），调用方跳过。

    ★ 先在临时画布上按基线画好整个字形，再整体「缩放」到目标框内。
      不能直接按 bearing 贴 —— 汉字字身常比点阵大（16px 框里行高 15~18px），
      直接贴会把顶部或底部裁掉（实测「数」「机」各少了 3 行笔画）。
    """
    if face.get_char_index(ord(ch)) == 0:
        return None

    face.set_pixel_sizes(CN_W, CN_H)
    face.load_char(ch, freetype.FT_LOAD_RENDER)
    bmp = face.glyph.bitmap
    left = face.glyph.bitmap_left
    top = face.glyph.bitmap_top

    # ① 画到大画布上（带 4px 余量，绝不裁字）
    PAD = 4
    bw, bh = CN_W + 2 * PAD, CN_H + 2 * PAD
    big = [[0] * bw for _ in range(bh)]
    # 基线放在 y = PAD + CN_H*0.8 处（汉字视觉重心偏下，留出上伸部）
    base = PAD + int(CN_H * 0.85)
    for y in range(bmp.rows):
        for x in range(bmp.width):
            if bmp.buffer[y * bmp.pitch + x] >= BIN_THRESHOLD:
                cy = base - top + y
                cx = PAD + left + x
                if 0 <= cy < bh and 0 <= cx < bw:
                    big[cy][cx] = 1

    # ② 求真实外接框
    ys = [y for y in range(bh) if any(big[y])]
    xs = [x for x in range(bw) if any(big[y][x] for y in range(bh))]
    if not ys or not xs:
        return [[0] * CN_W for _ in range(CN_H)]          # 空白字（如空格）
    y0, y1, x0, x1 = ys[0], ys[-1], xs[0], xs[-1]
    gw, gh = x1 - x0 + 1, y1 - y0 + 1

    # ③ 放进目标框：不放大（避免变形），只做居中 + 超框时等比缩小
    canvas = [[0] * CN_W for _ in range(CN_H)]
    if gw <= CN_W and gh <= CN_H:
        ox = (CN_W - gw) // 2                              # 水平居中
        oy = (CN_H - gh) // 2                              # 垂直居中
        for y in range(gh):
            for x in range(gw):
                canvas[oy + y][ox + x] = big[y0 + y][x0 + x]
    else:
        # 超框：按最长边等比缩到框内（最近邻，保笔画连通性）
        sc = min(CN_W / gw, CN_H / gh)
        tw, th = max(1, int(gw * sc)), max(1, int(gh * sc))
        ox, oy = (CN_W - tw) // 2, (CN_H - th) // 2
        for y in range(th):
            for x in range(tw):
                sy = y0 + int(y / sc)
                sx = x0 + int(x / sc)
                if big[sy][sx]:
                    canvas[oy + y][ox + x] = 1
    return canvas


def pack(canvas):
    """把画布打包成 (CN_H/8)*CN_W 字节（纵向取模、低位在上），与 font.c 一致。"""
    # 每 8 行一组（band），组内每列一个字节
    out = []
    for band in range(CN_H // 8):
        y0 = band * 8
        for x in range(CN_W):
            byte = 0
            for b in range(8):
                if canvas[y0 + b][x]:
                    byte |= (1 << b)    # bit0 在最上
            out.append(byte)
    assert len(out) == CN_BYTES, len(out)
    return out


def format_row(data, per_line=16):
    """格式化成 C 数组元素，外层加花括号（二维数组每个元素自带 {}）。"""
    items = ["0x%02X" % b for b in data]
    lines = []
    for i in range(0, len(items), per_line):
        lines.append("    " + ",".join(items[i:i + per_line]) + ",")
    body = "\n".join(lines)
    return "{\n" + body + "  },"


def main():
    global BIN_THRESHOLD          # 声明放最前：下面要用它的当前值做默认参数

    ap = argparse.ArgumentParser()
    ap.add_argument("ttf", help="TTF/TTC 字体文件")
    ap.add_argument("charset", help="字符集文本文件（UTF-8）")
    ap.add_argument("out", nargs="?", default="font_cn.c", help="输出 .c 文件")
    ap.add_argument("name", nargs="?", default="FontCN", help="C 数组名")
    ap.add_argument("--size", type=int, default=16,
                    help="点阵边长（8 的倍数，默认 16）。20 字更清楚但更占空间")
    # ★ default 必须取模块级 BIN_THRESHOLD 的值（不是写死的 128）。
    #   曾经 default 写死 128，而 argparse 总会把默认值传进来，
    #   于是上面那个「100」根本没用上，生成的还是缺笔字库 —— 踩过这个坑。
    ap.add_argument("--threshold", type=int, default=BIN_THRESHOLD,
                    help="灰度二值化阈值（默认 %d）。细字体调低可减少缺笔"
                         % BIN_THRESHOLD)
    args = ap.parse_args()

    set_size(args.size)
    BIN_THRESHOLD = args.threshold

    chars = collect_chars(args.charset)
    face = freetype.Face(args.ttf)

    entries = []      # (char, packed32)
    missing = []
    for ch in chars:
        cv = render_glyph(face, ch)
        if cv is None:
            missing.append(ch)
            continue
        entries.append((ch, pack(cv)))

    # ── 生成 C 文件 ────────────────────────────────────────────────────────
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("/**\n")
        f.write("  ******************************************************************************\n")
        f.write("  * @file    %s\n" % os.path.basename(args.out))
        f.write("  * @brief   中文字库（本文件由 tools/font_export.py 自动生成，请勿手改）\n")
        f.write("  *\n")
        f.write("  *          字体源：%s\n" % os.path.basename(args.ttf))
        f.write("  *          字数  ：%d 个\n" % len(entries))
        f.write("  *          点阵  ：%dx%d，%d 字节/字（格式同 font.c，纵向取模、低位在上）\n"
                % (CN_W, CN_H, CN_BYTES))
        f.write("  ******************************************************************************\n")
        f.write("  */\n")
        f.write('#include "font_cn.h"\n\n')
        f.write("static const uint8_t %s_Tab[%d][FONT_CN_BYTES] = {\n"
                % (args.name, len(entries)))
        for ch, data in entries:
            f.write("  /* %s (U+%04X) */\n" % (ch, ord(ch)))
            f.write(format_row(data) + "\n")
        f.write("};\n\n")

        # 索引表：码点升序，二分查找即可（也可线性，字少无所谓）
        f.write("static const uint16_t %s_Idx[%d] = {\n" % (args.name, len(entries)))
        for i in range(0, len(entries), 12):
            row = ["0x%04X" % ord(c) for c, _ in entries[i:i + 12]]
            f.write("    " + ",".join(row) + ",\n")
        f.write("};\n\n")

        f.write("const uint8_t* FONT_GetChinese(uint16_t code)\n{\n")
        f.write("    /* 线性查找：字库很小（几十~几百字），足够快；量大可改二分。 */\n")
        f.write("    for (int i = 0; i < %d; i++) {\n" % len(entries))
        f.write("        if (%s_Idx[i] == code) return %s_Tab[i];\n" % (args.name, args.name))
        f.write("    }\n")
        f.write("    return 0;\n")
        f.write("}\n")

    print("生成 %s ：%d 个字" % (args.out, len(entries)))
    if missing:
        print("字体中缺以下字符（已跳过）：%s" % "".join(missing))


if __name__ == "__main__":
    main()
