// 询问可答性审计：按 app.js 的可点击规则，检查每个询问是否存在任何可提交路径。
// 软锁 = 没有任何可点击候选 且 不允许放弃。
'use strict';
const path = require('path');
process.chdir(path.join(__dirname, '..', 'repro'));

let curAsk = null, askCount = 0, lastAskTime = 0, locks = [];
const log = [];
function P(s) { log.push(s); if (log.length > 300) log.shift(); }

globalThis.sgsOnLog = s => P('LOG ' + s);
globalThis.sgsOnState = json => { globalThis.__state = JSON.parse(json); };
globalThis.sgsOnEvent = () => {};
globalThis.sgsOnAskClosed = () => {};
globalThis.sgsOnAsk = json => {
  const a = JSON.parse(json);
  curAsk = a; askCount++; lastAskTime = Date.now();
  const st = globalThis.__state;
  const humanSeat = 0;
  const audit = { clickable: [], cancel: a.allowCancel, kind: a.kind, tag: a.tag };
  if (st) {
    const me = st.players.find(p => p.id === humanSeat);
    const handIds = new Set((me ? me.hand : []).map(c => c.id));
    const boardIds = new Set(st.players.flatMap(p => [...p.equip, ...p.judge].map(c => c.id)));
    for (const c of a.cards) {
      if (handIds.has(c.id) || boardIds.has(c.id)) audit.clickable.push(c.id);
    }
    // choice panel: 不在我手也不在牌面（模拟 renderChoices 的 outside）
    // 注意 app.js 的 outside 只排除手/牌面，仍可点
    if (a.kind === 'option') audit.clickable.push('(options)');
  }
  // 可答性判定（模拟 interaction.valid + submit 路径）
  let answerable = false;
  if (a.allowCancel) answerable = true;
  if (a.kind === 'option' && a.options.length > 0) answerable = true;
  if (a.kind === 'card' && a.cards.some(c => audit.clickable.includes(c.id))) answerable = true;
  if (a.kind === 'multi' && a.cards.some(c => audit.clickable.includes(c.id))) answerable = true;
  if (a.kind === 'target' && a.targets.length > 0) answerable = true;
  if (!answerable) {
    locks.push({ ask: a, state: st });
    P(`!! 软锁 ASK#${a.requestId} kind=${a.kind} tag=${a.tag} allowCancel=${a.allowCancel} cards=${JSON.stringify(a.cards)}`);
  }
  // 自动应答
  const ans = { ready: true, requestId: a.requestId, option: -1, cards: [], targets: [] };
  if (a.tag === 'Play') {
    let idx = a.options.findIndex(o => o.includes('顺手牵羊') || o.includes('过河拆桥'));
    if (idx < 0) idx = 0;
    ans.option = idx;
  } else if (a.kind === 'card' && a.cards.length) {
    ans.cards = [a.cards[0].id];
  } else if (a.kind === 'target' && a.targets.length) {
    ans.targets = [a.targets[0].id];
  } else if (a.kind === 'multi') {
    ans.cards = a.cards.slice(0, Math.max(1, a.minCards)).map(c => c.id);
  } else if (a.kind === 'option') {
    ans.option = a.options.length ? 0 : -1;
  }
  globalThis.sgsAnswer = ans;
};

const createModule = require(path.join(process.cwd(), 'sgs.js'));
createModule().then(m => {
  m._sgs_start(Number(process.argv[2] || 4242), 0, 4, 1);
});
let lastCount = 0;
setInterval(() => {
  const st = globalThis.__state;
  if (st && st.over) {
    console.log(`GAME OVER asks=${askCount} 软锁=${locks.length}`);
    if (locks.length) console.log(JSON.stringify(locks.map(l => l.ask), null, 1).slice(0, 3000));
    process.exit(locks.length ? 2 : 0);
  }
  if (askCount === lastCount && askCount > 0 && Date.now() - lastAskTime > 6000) {
    console.log('=== 等待超时（真卡死） ===');
    console.log(JSON.stringify(curAsk, null, 1).slice(0, 3000));
    process.exit(3);
  }
  lastCount = askCount;
}, 1500);
setTimeout(() => { console.log('超时 asks=', askCount, '软锁=', locks.length); process.exit(4); }, 90000);
