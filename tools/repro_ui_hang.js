// 新 UI 卡死复现（node + dist/sgs.wasm）：
// 人类座位 = 0，自动应答器策略性挑起 Snatch/Dismantle 目标空牌场景，
// 检测 wasm 永久等待（无 ask 超时未答 = 卡死）。
'use strict';
const path = require('path');
const fs = require('fs');

process.chdir(path.join(__dirname, '..', 'repro'));

let curAsk = null;
let askCount = 0;
let lastAskTime = 0;
const log = [];
function P(s) { log.push(s); if (log.length > 400) log.shift(); }

globalThis.sgsOnLog = s => P('LOG ' + s);
globalThis.sgsOnState = json => {
  const st = JSON.parse(json);
  globalThis.__state = st;
};
globalThis.sgsOnEvent = () => {};
globalThis.sgsOnAskClosed = () => {};
globalThis.sgsOnAsk = json => {
  const a = JSON.parse(json);
  curAsk = a;
  askCount++;
  lastAskTime = Date.now();
  P(`ASK#${a.requestId} kind=${a.kind} tag=${a.tag} cards=${a.cards.length} opts=${a.options.length} allowCancel=${a.allowCancel} min=${a.minCards}/${a.maxCards}`);
  // 自动应答策略
  const ans = { ready: true, requestId: a.requestId, option: -1, cards: [], targets: [] };
  if (a.tag === 'Play') {
    // 找 Snatch/Dismantle 选项；若对手可被牵/拆就选它，否则挑第一个能选的，否则放弃
    let idx = a.options.findIndex(o => o.includes('顺手牵羊') || o.includes('过河拆桥'));
    if (idx < 0) idx = 0;   // 否则永远出第一张（加速消耗手牌/装备）
    ans.option = idx;
    const od = a.optionDetails && a.optionDetails[idx];
    P(`  -> 选 ${a.options[idx]} targets=${JSON.stringify(od && od.targets)}`);
    globalThis.sgsAnswer = ans;
    return;
  }
  if (a.kind === 'card') {
    // Dismantle/Snatch pick：从 ask.cards 里挑一个（engine 会校验）
    if (a.cards.length > 0) {
      ans.cards = [a.cards[0].id];
      P(`  -> 选牌 id=${a.cards[0].id} area=${a.cards[0].area} hidden=${a.cards[0].hidden}`);
    } else {
      P('  !! card 询问候选为空 —— 无法作答（模拟卡死条件）');
      // 不作答：观察 wasm 是否永久等待
      return;
    }
    globalThis.sgsAnswer = ans;
    return;
  }
  if (a.kind === 'target') {
    if (a.targets.length > 0) ans.targets = [a.targets[0].id];
    globalThis.sgsAnswer = ans;
    return;
  }
  if (a.kind === 'multi') {
    ans.cards = a.cards.slice(0, Math.max(1, a.minCards)).map(c => c.id);
    globalThis.sgsAnswer = ans;
    return;
  }
  // option 类：Harvest 等选 0
  ans.option = a.options.length > 0 ? 0 : -1;
  globalThis.sgsAnswer = ans;
};

// 启动：human 座位 0
const createModule = require(path.join(process.cwd(), 'sgs.js'));
let Module;
createModule().then(m => {
  Module = m;
  P('wasm loaded');
  if (m._sgs_start) {
    m._sgs_start(4242, 0, 4, 1);   // seed, humanSeat=0, hp=4, bagua=ask
    P('started sgs_start(0,4242)');
  } else {
    P('exports: ' + Object.keys(m).filter(k => k.startsWith('_sgs')).join(','));
  }
});

// 看门狗：10 秒无新 ask 且未 over = 疑似卡死
let lastCount = 0;
setInterval(() => {
  const st = globalThis.__state;
  if (st && st.over) { console.log('GAME OVER. asks=', askCount); dump(); process.exit(0); }
  if (askCount === lastCount && askCount > 0) {
    const idle = Date.now() - lastAskTime;
    if (idle > 6000) {
      console.log('=== 疑似卡死 ===');
      console.log('当前 ask:', JSON.stringify(curAsk, null, 1));
      dump();
      process.exit(2);
    }
  }
  lastCount = askCount;
}, 2000);

function dump() {
  console.log('--- 最近日志 ---');
  console.log(log.slice(-40).join('\n'));
}
setTimeout(() => { console.log('超时未终局（可能对局太长） asks=', askCount); dump(); process.exit(3); }, 120000);
