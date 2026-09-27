/* ==========================================================================
   课程表编辑器 —— 前端逻辑
   与后端（tools/server.py）通过 /api/* 通信。
   ========================================================================== */

'use strict';

// ── 全局状态 ────────────────────────────────────────────────────────────
const S = {
  table: { week: 1, courses: [] },   // 当前编辑中的课表
  dirty: false,                      // 有未保存改动
  editing: null,                     // 正在编辑的课程下标（null = 新增）
  draft: null,                       // 弹窗里正在编辑的副本
  meta: {},                          // 后端返回的元信息（名字表、工具状态…）
};

const DAY_DEFAULT    = ['周一', '周二', '周三', '周四', '周五'];
const PERIOD_DEFAULT = ['上午一', '上午二', '下午一', '下午二', '晚上'];

const $  = (id) => document.getElementById(id);

// ── 小工具 ──────────────────────────────────────────────────────────────

/** 极简 HTML 转义。所有用户输入都要经过它再插入 DOM。 */
function esc(s) {
  return String(s == null ? '' : s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

/** 房间号：与固件一致 —— 只取数字部分（主楼302 → 302）。 */
function roomOf(place) {
  if (!place) return '';
  const m = place.match(/\d+/);
  return m ? m[0] : place;
}

async function api(path, body) {
  const opt = body
    ? { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body) }
    : {};
  const r = await fetch(path, opt);
  let data = {};
  try { data = await r.json(); } catch (_) { /* 允许空响应 */ }
  if (!r.ok && data && data.errors) return data;      // 校验错误：交给调用方
  if (!r.ok) throw new Error(data.message || ('HTTP ' + r.status));
  return data;
}

function toast(msg, kind) {
  const el = document.createElement('div');
  el.className = 'toast' + (kind ? ' ' + kind : '');
  el.textContent = msg;
  $('toasts').appendChild(el);
  setTimeout(() => {
    el.style.transition = 'opacity .25s';
    el.style.opacity = '0';
    setTimeout(() => el.remove(), 260);
  }, kind === 'err' ? 4200 : 2400);
}

// ── 冲突检测（与后端同规则，用于即时反馈）──────────────────────────────

function conflictSet(courses) {
  const bad = new Set();
  for (let i = 0; i < courses.length; i++) {
    for (let j = i + 1; j < courses.length; j++) {
      const a = courses[i], b = courses[j];
      if (a.day !== b.day) continue;
      if (b.period < a.period + a.span && a.period < b.period + b.span) {
        bad.add(i); bad.add(j);
      }
    }
  }
  return bad;
}

/** 找出与 candidate 冲突的课程下标数组。 */
function conflictsWith(courses, cand, skipIndex) {
  const out = [];
  courses.forEach((c, i) => {
    if (i === skipIndex) return;
    if (c.day !== cand.day) return;
    if (cand.period < c.period + c.span && c.period < cand.period + cand.span) {
      out.push(i);
    }
  });
  return out;
}

// ══════════════════════════════════════════════════════════════════════
// 网格渲染
// ══════════════════════════════════════════════════════════════════════

/* 墨水屏上：每格 69×56px，节次列 52px。保持这个比例，屏幕上的网格
 * 才和实际显示效果一致（不然看着宽窄比例是错的）。 */
const CELL_RATIO = 69 / 56;          // 格子宽/高
const COL_OVER_CELL = 52 / 69;       // 节次列宽 / 格子宽
const CELL_MIN_W = 96;               // 太窄就不好点，设个下限
const CELL_MAX_W = 185;              // 上限：再大格子会显得空，但太小时大片留白也难看

/** 按可用宽高算出一套网格尺寸，写进 CSS 变量。
 *  宽度和高都要照顾：只按宽度放大会让格子变得很高，底部留一大片空白。 */
function layoutGrid() {
  const wrap = document.querySelector('.grid-wrap');
  if (!wrap) return;

  const avail = wrap.clientWidth - 32 - 2;
  const availH = wrap.clientHeight - 32 - 2;

  // ── 按宽度算 ──
  let cellW = avail / (5 + COL_OVER_CELL);

  // ── 按高度反推 ──
  // 总高 = 表头 + 5 个格子 = cellH * (headRatio + 5)
  // 其中 headRatio = 20/56 = 表头高 / 格子高
  const headRatio = 20 / 56;
  if (availH > 100) {
    const cellHByHeight = availH / (5 + headRatio);
    const cellWByHeight = cellHByHeight * CELL_RATIO;
    if (cellWByHeight < cellW) cellW = cellWByHeight;   // 高度更紧，听高度的
  }

  // 上下限：太窄点不动，太宽只是白白拉长（内容并不会变多）
  cellW = Math.max(CELL_MIN_W, Math.min(CELL_MAX_W, cellW));

  const cellH = cellW / CELL_RATIO;
  const colW  = cellW * COL_OVER_CELL;
  const headH = Math.round(cellH * headRatio);

  const root = document.documentElement.style;
  root.setProperty('--cell-w', cellW.toFixed(1) + 'px');
  root.setProperty('--cell-h', cellH.toFixed(1) + 'px');
  root.setProperty('--col-w',  colW.toFixed(1) + 'px');
  root.setProperty('--head-h', headH + 'px');

  // 字体也跟着放大，否则格子变大后字显得很小
  // 以 cellW=118 时的字号为基准线性缩放，限制在合理区间
  const k = cellW / 118;
  root.setProperty('--f-name',  Math.max(12, Math.min(18, 13 * k)).toFixed(1) + 'px');
  root.setProperty('--f-place', Math.max(10.5, Math.min(15, 11.5 * k)).toFixed(1) + 'px');
  root.setProperty('--f-head',  Math.max(12, Math.min(16, 13 * k)).toFixed(1) + 'px');
}

function renderGrid() {
  layoutGrid();                       // 先定尺寸，再画（气泡高度依赖它）
  const days    = S.meta.dayNames    || DAY_DEFAULT;
  const periods = S.meta.periodNames || PERIOD_DEFAULT;
  const courses = S.table.courses;
  const bad     = conflictSet(courses);

  // 先算出每个格子被哪门课占了（用于区分「空」和「被跨节覆盖」）
  const owner = {};                       // "day,period" -> 课程下标
  courses.forEach((c, i) => {
    for (let k = 0; k < c.span; k++) owner[c.day + ',' + (c.period + k)] = i;
  });

  const tbl = $('grid');
  tbl.innerHTML = '';

  // ── 表头：左上角是周次，其余是星期 ──
  const thead = document.createElement('thead');
  const htr = document.createElement('tr');

  const corner = document.createElement('th');
  corner.className = 'corner';
  corner.textContent = S.table.week + '周';
  htr.appendChild(corner);

  days.forEach((d) => {
    const th = document.createElement('th');
    th.className = 'day';
    th.textContent = d;
    htr.appendChild(th);
  });
  thead.appendChild(htr);
  tbl.appendChild(thead);

  // ── 表体 ──
  const tbody = document.createElement('tbody');

  periods.forEach((pName, pi) => {
    const tr = document.createElement('tr');

    const th = document.createElement('th');
    th.className = 'period';
    th.textContent = pName;
    tr.appendChild(th);

    days.forEach((_, di) => {
      const td = document.createElement('td');
      td.className = 'slot';
      td.dataset.day = di;
      td.dataset.period = pi;

      const idx = owner[di + ',' + pi];

      if (idx === undefined) {
        // 空格子
        td.classList.add('empty');
        td.addEventListener('click', () => openEditor(null, di, pi));
      } else {
        const c = courses[idx];
        if (c.period !== pi) {
          // 被上面某门跨节课程盖住 —— 不重复画，也不响应点击
          td.classList.add('covered');
        } else {
          // 这是气泡的起点：画一个跨行高的块
          const b = document.createElement('div');
          b.className = 'bubble' + (bad.has(idx) ? ' conflict' : '');
          b.innerHTML =
            '<div class="b-name">' + esc(c.name) + '</div>' +
            (roomOf(c.place)
              ? '<div class="b-place">' + esc(roomOf(c.place)) + '</div>'
              : '') +
            (c.span > 1 ? '<span class="b-span">' + c.span + '节</span>' : '');
          b.title = c.name + (c.place ? ' @ ' + c.place : '') +
                    '（' + (days[c.day]) + ' ' +
                    periods[c.period] +
                    (c.span > 1 ? '~' + periods[c.period + c.span - 1] : '') + '）';
          // 高度 = span 个格子（含格线）
          b.style.height = 'calc(' + c.span + ' * var(--cell-h) - 8px)';
          b.addEventListener('click', (e) => {
            e.stopPropagation();
            openEditor(idx, di, pi);
          });
          td.appendChild(b);
        }
      }
      tr.appendChild(td);
    });
    tbody.appendChild(tr);
  });

  tbl.appendChild(tbody);

  // 冲突徽标
  $('conflictBadge').hidden = bad.size === 0;
}

// ══════════════════════════════════════════════════════════════════════
// 检查清单
// ══════════════════════════════════════════════════════════════════════

function renderChecklist(missingChars, tools) {
  const items = [];
  const bad = conflictSet(S.table.courses);
  const t = tools || S.meta.tools || {};

  const li = (kind, text) =>
    '<li class="' + kind + '"><span class="ci">' +
    (kind === 'ok' ? '✓' : kind === 'warn' ? '!' : '×') +
    '</span><span>' + text + '</span></li>';

  // 课程与冲突
  if (S.table.courses.length === 0) {
    items.push(li('warn', '课表还是空的 —— 点网格里的空格子添加课程'));
  } else if (bad.size) {
    items.push(li('bad', '有 ' + bad.size + ' 门课时间冲突（红框标出），请先解决'));
  } else {
    items.push(li('ok', S.table.courses.length + ' 门课，无时间冲突'));
  }

  // 周次
  items.push(li('ok', '第 ' + S.table.week + ' 周'));

  // 字库
  const mc = missingChars || [];
  if (mc.length) {
    items.push(li('warn', '有 ' + mc.length + ' 个新汉字（' + esc(mc.join('')) +
                        '），生成时会自动补进字库'));
  } else {
    items.push(li('ok', '字库已覆盖所有用字' + (S.meta.fontChars
                        ? '（当前 ' + S.meta.fontChars + ' 字）' : '')));
  }

  // 设置是否完整（生成代码所必需的）
  if (t.python === false) items.push(li('bad', 'Python 解释器路径无效（设置里可改）'));
  if (t.font === false)   items.push(li('bad', '字体文件不存在（设置里可改）'));

  // 编译/烧录是可选能力：只提示状态，不当错误
  const canBuild = t.build_set, canFlash = t.flash_set;
  if (!canBuild || !canFlash) {
    items.push(li('warn',
      '未配置' + (!canBuild ? '编译' : '') + (!canBuild && !canFlash ? '和' : '') +
      (!canFlash ? '烧录' : '') + '命令 —— 网页只生成代码，' +
      '需你自己在终端编译烧录（可在设置里配）'));
  } else {
    const okCm = t.cmake ? '' : '（PATH 里没找到 cmake）';
    const okOc = t.openocd ? '' : '（PATH 里没找到 openocd）';
    items.push(li(t.cmake && t.openocd ? 'ok' : 'warn',
      '已配置编译和烧录命令' + okCm + okOc));
  }

  $('checklist').innerHTML = items.join('');
}

// ══════════════════════════════════════════════════════════════════════
// 编辑弹窗
// ══════════════════════════════════════════════════════════════════════

function openEditor(index, day, period) {
  S.editing = index;

  if (index === null) {
    // 新增：用点击的格子作默认值
    S.draft = { day: day, period: period, span: 1, name: '', place: '' };
    $('modalTitle').textContent = '添加课程';
    $('btnDelete').hidden = true;
  } else {
    S.draft = Object.assign({}, S.table.courses[index]);
    $('modalTitle').textContent = '编辑课程';
    $('btnDelete').hidden = false;
  }

  $('inName').value  = S.draft.name;
  $('inPlace').value = S.draft.place;
  $('modalAlert').hidden = true;
  $('btnConfirm').disabled = false;

  renderSegs();
  $('modalBackdrop').hidden = false;
  setTimeout(() => $('inName').focus(), 40);
}

function closeEditor() {
  $('modalBackdrop').hidden = true;
  S.draft = null;
  S.editing = null;
}

/** 渲染星期/起始节/持续节数的分段选择器。 */
function renderSegs() {
  if (!S.draft) return;
  const days    = S.meta.dayNames    || DAY_DEFAULT;
  const periods = S.meta.periodNames || PERIOD_DEFAULT;

  // 星期
  $('segDay').innerHTML = days.map((d, i) =>
    '<button type="button" data-v="' + i + '"' +
    (S.draft.day === i ? ' class="on"' : '') + '>' + esc(d) + '</button>'
  ).join('');

  // 起始节
  $('segPeriod').innerHTML = periods.map((p, i) =>
    '<button type="button" data-v="' + i + '"' +
    (S.draft.period === i ? ' class="on"' : '') + '>' + esc(p) + '</button>'
  ).join('');

  // 持续节数：最多到最后一节
  const maxSpan = 5 - S.draft.period;
  let spanHtml = '';
  for (let k = 1; k <= maxSpan; k++) {
    spanHtml += '<button type="button" data-v="' + k + '"' +
      (S.draft.span === k ? ' class="on"' : '') + '>' + k + ' 节</button>';
  }
  $('segSpan').innerHTML = spanHtml;

  // 提示跨度含义
  const endP = S.draft.period + S.draft.span - 1;
  $('spanHint').textContent = S.draft.span > 1
    ? '连堂：' + periods[S.draft.period] + ' ~ ' + periods[endP] + '，屏幕上会连成一块气泡'
    : '单节：' + periods[S.draft.period];

  // 绑定（每次重渲染都要重新绑）
  [['segDay', 'day'], ['segPeriod', 'period'], ['segSpan', 'span']].forEach(([id, key]) => {
    $(id).querySelectorAll('button').forEach((btn) => {
      btn.addEventListener('click', () => {
        const v = parseInt(btn.dataset.v, 10);
        S.draft[key] = v;
        // 起始节变了可能导致跨度越界，收一下
        if (S.draft.period + S.draft.span > 5) {
          S.draft.span = 5 - S.draft.period;
        }
        renderSegs();
        liveCheck();
      });
    });
  });

  liveCheck();
}

/** 弹窗内的即时校验。 */
function liveCheck() {
  if (!S.draft) return;

  const alertEl = $('modalAlert');
  const name = $('inName').value.trim();

  // 名称长度（按屏幕能显示的行数给个软提醒，不阻止）
  const nameHint = $('nameHint');
  const len = [...name].length;
  if (len === 0) {
    nameHint.textContent = '必填';
    nameHint.style.color = 'var(--warn)';
  } else if (len > 8) {
    nameHint.textContent = '较长（' + len + ' 字），屏幕上可能会换行或截断';
    nameHint.style.color = 'var(--warn)';
  } else {
    nameHint.textContent = '';
  }

  // 冲突检查
  const cand = Object.assign({}, S.draft, { name: name });
  const hits = conflictsWith(S.table.courses, cand, S.editing);
  if (hits.length) {
    const names = hits.map((i) => '「' + S.table.courses[i].name + '」').join('、');
    alertEl.className = 'alert';
    alertEl.innerHTML = '<b>时间冲突</b>：与 ' + esc(names) +
      ' 重叠。请换时段，或先删掉那门课。';
    alertEl.hidden = false;
  } else {
    alertEl.hidden = true;
  }

  $('btnConfirm').disabled = (len === 0) || hits.length > 0;
}

function confirmEditor() {
  if (!S.draft) return;
  const name = $('inName').value.trim();
  if (!name) return;

  const cand = {
    day: S.draft.day, period: S.draft.period, span: S.draft.span,
    name: name, place: $('inPlace').value.trim(),
  };

  if (S.editing === null) {
    S.table.courses.push(cand);
  } else {
    S.table.courses[S.editing] = cand;
  }

  S.dirty = true;
  closeEditor();
  refreshLocal();
  toast(S.editing === null ? '已添加' : '已修改', 'ok');
}

function deleteCourse() {
  if (S.editing === null) return;
  const c = S.table.courses[S.editing];
  if (!confirm('删除「' + c.name + '」？')) return;

  S.table.courses.splice(S.editing, 1);
  S.dirty = true;
  closeEditor();
  refreshLocal();
  toast('已删除', 'ok');
}

/** 本地刷新（不重新请求后端，用于编辑后的即时反馈）。 */
function refreshLocal() {
  renderGrid();
  renderChecklist(S.meta.missingChars, S.meta.tools);
  updateDirtyUI();
  updateDeployUI();
}

/** 根据「编译/烧录命令是否配了」调整按钮文案与说明。
 *  未配置时按钮只生成代码 —— 不假装能做实际上做不到的事。 */
function updateDeployUI() {
  const t = S.meta.tools || {};
  const steps = ['保存数据', '补字库', '生成 my_courses.c'];
  if (t.build_set) steps.push('编译');
  if (t.flash_set) steps.push('烧录');

  $('deployNote').textContent = steps.join(' → ');
  $('deployLabel').textContent = t.flash_set ? '编译并烧录'
                               : t.build_set ? '编译'
                               : '生成代码';
}

function updateDirtyUI() {
  const btn = $('btnSave');
  btn.textContent = S.dirty ? '保存 *' : '保存';
  btn.classList.toggle('primary', true);
}

// ══════════════════════════════════════════════════════════════════════
// 与后端交互
// ══════════════════════════════════════════════════════════════════════

async function loadState() {
  const d = await api('/api/state');
  S.meta = d;
  S.table = d.table;
  S.dirty = false;

  $('rootPath').textContent = d.root;
  $('weekInput').value = S.table.week;

  refreshLocal();
}

async function doSave() {
  try {
    const d = await api('/api/save', { table: S.table });
    if (d.errors) { toast(d.errors[0], 'err'); return; }
    S.dirty = false;
    S.meta.missingChars = d.missingChars || [];
    S.meta.fontChars = d.fontChars;
    refreshLocal();
    toast('已保存并生成代码', 'ok');
  } catch (e) {
    toast('保存失败：' + e.message, 'err');
  }
}

async function doDeploy() {
  const btn = $('btnDeploy');
  const t = S.meta.tools || {};
  btn.disabled = true;
  $('progress').hidden = false;
  $('barFill').style.width = '8%';
  $('barFill').style.background = '';       // 复位上次失败留下的红色
  $('stageText').textContent = '保存数据…';

  // 分阶段推进进度条（后端是一次调用，这里按已知阶段做视觉反馈）
  const stages = [[20, '生成 my_courses.c…'], [40, '检查字库…']];
  if (t.build_set) stages.push([65, '编译…']);
  if (t.flash_set) stages.push([85, '烧录…']);
  let si = 0;
  const timer = setInterval(() => {
    if (si < stages.length) {
      $('barFill').style.width = stages[si][0] + '%';
      $('stageText').textContent = stages[si][1];
      si++;
    }
  }, 700);

  try {
    const d = await api('/api/deploy', { table: S.table });
    clearInterval(timer);

    if (d.log) showLog('输出', d.log);

    if (d.errors) {
      $('barFill').style.width = '100%';
      $('barFill').style.background = 'var(--danger)';
      $('stageText').textContent = '有错误，见日志';
      toast(d.errors[0], 'err');
      return;
    }

    $('barFill').style.width = '100%';
    $('stageText').textContent = '完成';
    S.dirty = false;
    S.meta.missingChars = [];
    if (d.fontChars) S.meta.fontChars = d.fontChars;
    refreshLocal();
    // 只报实际做过的事，不夸大
    toast(t.flash_set ? '烧录完成，屏幕刷新中'
        : t.build_set ? '编译完成'
        : '已生成 my_courses.c，请自行编译', 'ok');

  } catch (e) {
    clearInterval(timer);
    $('barFill').style.width = '100%';
    $('barFill').style.background = 'var(--danger)';
    $('stageText').textContent = '失败';
    toast(e.message || '部署失败', 'err');
  } finally {
    btn.disabled = false;
  }
}

function showLog(title, text) {
  $('logTitle').textContent = title;
  $('logText').textContent = text;
  $('logbox').hidden = false;
}

// ══════════════════════════════════════════════════════════════════════
// 设置
// ══════════════════════════════════════════════════════════════════════

function openSettings() {
  const cfg = S.meta.config || {};
  $('setPython').value = cfg.python || '';
  $('setFont').value   = cfg.font   || '';
  $('setBuild').value  = cfg.build  || '';
  $('setFlash').value  = cfg.flash  || '';

  const t = S.meta.tools || {};
  const rows = [
    ['python',  'Python 解释器'],
    ['font',    '字体文件'],
    ['src',     'Core/Src 目录'],
    ['cmake',   'cmake（编译用，可选）'],
    ['openocd', 'openocd（烧录用，可选）'],
  ];
  $('toolList').className = 'alert info';
  $('toolList').innerHTML = '<b>环境自检</b><br>' + rows.map(([k, label]) =>
    (t[k] ? '✓ ' : '× ') + label
  ).join('<br>');

  $('setBackdrop').hidden = false;
}

async function saveSettings() {
  try {
    const d = await api('/api/config', {
      python: $('setPython').value.trim(),
      font:   $('setFont').value.trim(),
      build:  $('setBuild').value.trim(),
      flash:  $('setFlash').value.trim(),
    });
    S.meta.config = d.config;
    S.meta.tools = d.tools;
    $('setBackdrop').hidden = true;
    refreshLocal();
    toast('设置已保存', 'ok');
  } catch (e) {
    toast('保存设置失败：' + e.message, 'err');
  }
}

// ══════════════════════════════════════════════════════════════════════
// 事件绑定
// ══════════════════════════════════════════════════════════════════════

function bind() {
  // 周次
  const setWeek = (v) => {
    v = Math.max(1, Math.min(99, v | 0));
    S.table.week = v;
    $('weekInput').value = v;
    S.dirty = true;
    refreshLocal();
  };
  $('weekInput').addEventListener('change',
    (e) => setWeek(parseInt(e.target.value, 10) || 1));
  $('weekMinus').addEventListener('click', () => setWeek(S.table.week - 1));
  $('weekPlus').addEventListener('click',  () => setWeek(S.table.week + 1));

  // 顶栏
  $('btnSave').addEventListener('click', doSave);
  $('btnReload').addEventListener('click', async () => {
    if (S.dirty && !confirm('放弃当前改动，重新从磁盘载入？')) return;
    await loadState();
    toast('已重新载入', 'ok');
  });
  $('btnSettings').addEventListener('click', openSettings);
  $('btnDeploy').addEventListener('click', doDeploy);

  // 弹窗
  $('modalClose').addEventListener('click', closeEditor);
  $('btnCancel').addEventListener('click', closeEditor);
  $('btnConfirm').addEventListener('click', confirmEditor);
  $('btnDelete').addEventListener('click', deleteCourse);
  $('modalBackdrop').addEventListener('click', (e) => {
    if (e.target === $('modalBackdrop')) closeEditor();
  });

  $('inName').addEventListener('input', liveCheck);
  $('inName').addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !$('btnConfirm').disabled) confirmEditor();
  });
  $('inPlace').addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !$('btnConfirm').disabled) confirmEditor();
  });

  // 设置弹窗
  $('setClose').addEventListener('click', () => { $('setBackdrop').hidden = true; });
  $('setCancel').addEventListener('click', () => { $('setBackdrop').hidden = true; });
  $('setSave').addEventListener('click', saveSettings);
  $('setBackdrop').addEventListener('click', (e) => {
    if (e.target === $('setBackdrop')) $('setBackdrop').hidden = true;
  });

  // 日志
  $('logClose').addEventListener('click', () => { $('logbox').hidden = true; });

  // 快捷键
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      if (!$('modalBackdrop').hidden) closeEditor();
      else if (!$('setBackdrop').hidden) $('setBackdrop').hidden = true;
      else if (!$('logbox').hidden) $('logbox').hidden = true;
      return;
    }
    // Ctrl/Cmd + S 保存
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
      e.preventDefault();
      doSave();
    }
  });

  // 离开前提醒
  window.addEventListener('beforeunload', (e) => {
    if (S.dirty) { e.preventDefault(); e.returnValue = ''; }
  });

  // 窗口尺寸变化时重算网格（防抖，避免拖动窗口时疯狂重排）
  let rzTimer = null;
  window.addEventListener('resize', () => {
    clearTimeout(rzTimer);
    rzTimer = setTimeout(() => renderGrid(), 120);
  });
}

// ── 启动 ────────────────────────────────────────────────────────────────
(async function main() {
  bind();
  try {
    await loadState();
  } catch (e) {
    document.querySelector('.grid-wrap').innerHTML =
      '<p class="loading">无法连接本地服务。<br>' +
      '请确认 tools/课表编辑器.bat 的窗口还开着。</p>';
  }
})();
