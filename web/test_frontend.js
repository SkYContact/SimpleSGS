/* ============================================================================
 *  前端端到端测试（不需要浏览器）
 *
 *  做三件事：
 *    1) 用 index.html 里真实的 id / data-* 搭一个迷你 DOM
 *    2) 原样加载 dist/app.js（页面真正跑的那份代码）
 *    3) 接上真实 wasm 引擎，模拟"玩家点按钮"打完一局
 *
 *  用法: node web/test_frontend.js [选项序号]
 * ========================================================================== */
'use strict';

const fs = require('fs');
const path = require('path');

const distDir = path.join(__dirname, 'dist');
const html = fs.readFileSync(path.join(distDir, 'index.html'), 'utf8');
const appJs = fs.readFileSync(path.join(distDir, 'app.js'), 'utf8');

process.chdir(distDir);                       // wasm 的 .wasm/.data 按 cwd 找

// ------------------------------------------------------------------ 迷你 DOM
const byId = new Map();
const listeners = {};

function makeEl(id = '') {
  const el = {
    id,
    tagName: 'DIV',
    _innerHTML: '',
    textContent: '',
    className: '',
    value: '1',
    disabled: false,
    scrollTop: 0,
    scrollHeight: 0,
    dataset: {},
    style: {},
    children: [],
    onclick: null,
    classList: {
      add: (c) => { if (!el.className.split(' ').includes(c)) el.className += ' ' + c; },
      remove: (c) => { el.className = el.className.split(' ').filter((x) => x && x !== c).join(' '); },
      contains: (c) => el.className.split(' ').includes(c),
      toggle: (c) => (el.classList.contains(c) ? el.classList.remove(c) : el.classList.add(c)),
    },
    get innerHTML() { return el._innerHTML; },
    set innerHTML(v) {
      el._innerHTML = v;
      el.children = [];
      // 把内联创建的 id / data-* 元素挂进来（app.js 会随后按 id 去取按钮）
      for (const m of v.matchAll(/id="([^"]+)"/g)) {
        if (!byId.has(m[1])) byId.set(m[1], makeEl(m[1]));
      }
      for (const m of v.matchAll(/data-(card|opt|target|start)="(-?\d+)"/g)) {
        const child = makeEl();
        child.dataset[m[1]] = m[2];
        child.selector = `[data-${m[1]}]`;
        el.children.push(child);
      }
    },
    appendChild(c) { el.children.push(c); return c; },
    removeChild(c) { el.children = el.children.filter((x) => x !== c); return c; },
    get childElementCount() { return el.children.length; },
    get firstChild() { return el.children[0]; },
    remove() {},
    querySelector: () => null,
    querySelectorAll(sel) { return el.children.filter((c) => c.selector === sel); },
  };
  return el;
}

for (const m of html.matchAll(/id="([^"]+)"/g)) byId.set(m[1], makeEl(m[1]));

// index.html 里的三个开局按钮
const startButtons = [];
for (const m of html.matchAll(/data-start="(-?\d+)"/g)) {
  const b = makeEl(`start-${m[1]}`);
  b.dataset.start = m[1];
  b.selector = '[data-start]';
  startButtons.push(b);
}

global.document = {
  getElementById: (id) => byId.get(id) || null,
  querySelector: (sel) => (sel === '.result-banner' ? null : null),
  querySelectorAll: (sel) => (sel === '[data-start]' ? startButtons : []),
  createElement: () => makeEl(),
  addEventListener: (ev, fn) => { listeners[ev] = fn; },
  body: { appendChild: () => {}, removeChild: () => {} },
  querySelectorAllRaw: null,
};
global.window = globalThis;
global.location = { pathname: '/index.html', href: 'http://localhost:8000/index.html' };

// ------------------------------------------------------------------ 加载前端
let errors = [];
const origError = console.error;
console.error = (...a) => { errors.push(a.join(' ')); origError(...a); };

// 引擎工厂（和浏览器里 <script src="sgs.js"> 提供的是同一个）
global.createSgsModule = require(path.join(distDir, 'sgs.js'));

new Function(appJs)();               // 执行 app.js（它会挂 DOMContentLoaded）
listeners['DOMContentLoaded']();     // 触发 boot()

// ------------------------------------------------------------------ 驱动
const $id = (i) => byId.get(i);
let asks = 0;
let logLines = 0;

// 按询问类型"点"页面：单选的直接点；多选的先勾够再点确认（这正是页面上玩家的操作）
function tryClick(ask) {
  const bar = $id('actionBar');
  if (!bar || !ask) return;
  const pickAll = (sel) => bar.querySelectorAll(sel);
  const cards = pickAll('[data-card]');
  const opts = pickAll('[data-opt]');
  const targets = pickAll('[data-target]');

  const multiSelect = ask.kind === 'multi' || (ask.kind === 'target' && ask.maxTargets > 1);
  if (multiSelect) {
    const need = Math.max(1, ask.kind === 'multi' ? ask.minCards : ask.minTargets);
    const pool = ask.kind === 'multi' ? cards : targets;
    for (let i = 0; i < need && i < pool.length; i++) pool[i].onclick();
    const confirm = $id('askConfirm');
    if (confirm) confirm.onclick();
    return;
  }
  const el = cards[0] || opts[0] || targets[0];
  if (el) { el.onclick(); return; }
  if ($id('askCancel')) $id('askCancel').onclick();   // 结束出牌阶段 / 放弃
}

// 事件驱动：每次询问渲染完就立刻点（不要用轮询，长局会被 15ms 轮询拖死）
let done = false;
const trace = [];
const appOnAsk = globalThis.sgsOnAsk;
globalThis.sgsOnAsk = (json) => {
  if (appOnAsk) appOnAsk(json);
  if (done) return;
  asks++;
  const a = JSON.parse(json);
  trace.push(`#${asks} [${a.kind}/${a.tag}] ${a.prompt} | 选项${a.options.length} 牌${a.cards.length} 目标${a.targets.length}`);
  setTimeout(() => tryClick(a), 0);
};

const watchdog = setTimeout(() => {
  console.log(`\n❌ 超时：${asks} 次决策后仍未结束，最后 6 次询问：`);
  console.log(trace.slice(-6).join('\n'));
  process.exit(1);
}, 30000);

// 局面数据（app.js 的 sgsOnState 会写进棋盘 DOM）
const check = () => {
  const seat = $id('meSide');
  const foe = $id('foeSide');
  console.log(`\n=== 前端测试结果 ===`);
  console.log(`随机种子      : ${$id('seedInput').value}`);
  console.log(`决策次数      : ${asks}`);
  console.log(`日志行数      : ${logLines}`);
  console.log(`我的棋盘渲染  : ${seat && seat.innerHTML ? '有内容 (' + seat.innerHTML.length + ' 字符)' : '空!'}`);
  console.log(`对手棋盘渲染  : ${foe && foe.innerHTML ? '有内容 (' + foe.innerHTML.length + ' 字符)' : '空!'}`);
  console.log(`回合提示      : ${$id('turnInfo').textContent}`);
  const ok = asks > 5 && logLines > 30 && seat.innerHTML.length > 200 && errors.length === 0;
  if (errors.length) console.log('console.error: ' + errors.slice(0, 3).join(' | '));
  console.log(ok ? '\n✅ 前端端到端通过' : '\n❌ 有问题');
  process.exit(ok ? 0 : 1);
};

// ------------------------------------------------------------------ 勾住回调
const realOnDone = globalThis.sgsOnDone;
globalThis.sgsOnDone = () => {
  if (realOnDone) realOnDone();
  done = true;
  clearTimeout(watchdog);
  setTimeout(check, 30);
};
const realOnLog = globalThis.sgsOnLog;
globalThis.sgsOnLog = (s) => { logLines++; if (realOnLog) realOnLog(s); };

// 开一局人机（座位 0 = 玩家，点击由 tryClick 模拟）
setTimeout(() => {
  // --- 随机种子 / 换一个 按钮 ---
  const box = $id('seedInput');
  const before = box.value;
  if (!/^\d+$/.test(before) || Number(before) < 1) {
    errors.push(`种子不是合法随机数: "${before}"`);
  }
  if ($id('reseed')) {
    $id('reseed').onclick();
    if (box.value === before) errors.push('点「换一个」后种子没变');
    console.log(`随机种子：${before} → 点「换一个」→ ${box.value}`);
  } else {
    errors.push('找不到 reseed 按钮');
  }
  box.value = '424242';        // 固定一个种子，验证能复现
  globalThis.sgsSpeedMs = 0;   // 关掉 AI 的播放节奏（页面默认 260ms/步，测试不需要）
  // 设置面板：八卦阵改「每次询问」，验证"对局中随时改"这条链路（会覆盖到新的 option 询问）
  const sBtn = $id('settingsBtn');
  const sPanel = $id('settingsPanel');
  const bagua = $id('baguaSelect');
  if (!sBtn || !sPanel || !bagua) errors.push('设置面板元素缺失');
  else {
    // 默认必须是「每次询问我是否发动」（正式规则，与 QSanguosha 的 EightDiagramSkill 一致）
    if (bagua.value !== '1') errors.push('八卦阵默认档不是「询问」而是 ' + bagua.value);
    sBtn.onclick();                                   // 打开面板
    if (!sPanel.classList.contains('open')) errors.push('点设置按钮没打开面板');
    bagua.value = '1';
    if (!bagua.onchange) errors.push('baguaSelect 没绑 onchange');
    else bagua.onchange();
    if (Number(globalThis.sgsBaguaMode) !== 1) {
      errors.push('改设置没写进全局变量 sgsBaguaMode=' + globalThis.sgsBaguaMode);
    }
  }

  const btn = startButtons.find((b) => b.dataset.start === '0');
  if (!btn || !btn.onclick) { console.log('❌ 没绑定开局按钮'); process.exit(1); }
  console.log('点击「人机对战 · 你执先手」…');
  btn.onclick();
}, 20);
