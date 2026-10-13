/* ============================================================================
 *  三国杀 1v1 —— 网页前端
 *  C++ 内核编成 wasm 后：模块通过 window.sgsOnLog / sgsOnState / sgsOnAsk
 *  把对局过程推给页面；页面把玩家的选择写回 window.sgsAnswer，
 *  挂在 emscripten_sleep 上的 C++ 醒来后读走继续跑。
 * ========================================================================== */
'use strict';

const S = {
  module: null,
  over: true,
  humanSeat: -1,
  players: [],
  current: -1,
  pending: null,     // 当前等待玩家决策的 ask
  selected: new Set(),
  started: false,
};

const $ = (id) => document.getElementById(id);

// ---------------------------------------------------------------- 卡牌显示
const SUIT_RE = /\(([♠♥♣♦])([^)]*)\)/;

function cardHtml(label, extraClass = '') {
  const m = SUIT_RE.exec(label || '');
  let name = label || '';
  let suit = '';
  if (m) {
    name = label.slice(0, m.index).trim();
    suit = `<span class="suit">${m[1]}${m[2]}</span>`;
  }
  const red = m && (m[1] === '♥' || m[1] === '♦');
  const cls = ['card', red ? 'red' : 'black', extraClass].filter(Boolean).join(' ');
  return `<span class="${cls}">${name}${suit}</span>`;
}

function backCards(n) {
  let out = '';
  for (let i = 0; i < n; i++) out += '<span class="card back">?</span>';
  return out || '<span class="mini-title">（无手牌）</span>';
}

function hpDots(hp, maxHp) {
  let out = '';
  for (let i = 0; i < Math.max(hp, maxHp); i++) {
    out += `<span class="hp-dot ${i < hp ? '' : 'empty'}"></span>`;
  }
  return out;
}

// ---------------------------------------------------------------- 棋盘渲染
function renderBoard() {
  const players = S.players;
  if (!players.length) return;

  // players[0] 永远是先手方；把自己换到下面显示
  const bottom = S.humanSeat === 1 ? players[1] : players[0];
  const top = S.humanSeat === 1 ? players[0] : players[1];
  renderSeat($('foeSide'), top, top.id === S.current);
  renderSeat($('meSide'), bottom, bottom.id === S.current);

  $('turnInfo').textContent = `第 ${S.turn} 回合`;
  $('deckInfo').textContent = `牌堆 ${S.deck} · 弃牌 ${S.discard}`;
  const who = players.find(p => p.id === S.current);
  $('whoInfo').textContent = S.over ? '对局结束' : (who ? `${who.name} 的回合` : '结算中');
}

function renderSeat(el, p, active) {
  const isMe = S.humanSeat >= 0 && p.id === S.humanSeat;
  const handHtml = isMe
    ? (p.hand.length ? p.hand.map(c => cardHtml(c.label)).join('') : '<span class="mini-title">（无手牌）</span>')
    : backCards(p.handCount);

  const equipHtml = p.equip.length
    ? p.equip.map(e => `<span class="mini-title">${e.slot}</span>${cardHtml(e.label, 'equip')}`).join('&nbsp;&nbsp;')
    : '<span class="mini-title">（无装备）</span>';

  const judgeHtml = p.judge.length
    ? p.judge.map(c => cardHtml(c.label)).join('')
    : '<span class="mini-title">（判定区空）</span>';

  el.className = 'seat' + (active ? ' active' : '') + (p.alive ? '' : ' dead');
  el.innerHTML = `
    <div class="seat-head">
      <span class="seat-name">${p.name}</span>
      <span class="seat-tag">${isMe ? '你' : '对手'}</span>
      <span class="seat-tag">攻击范围 ${p.range}</span>
      <span class="seat-tag">距离 ${p.distance}</span>
      <span class="seat-hp">${hpDots(p.hp, p.maxHp)}<span class="hp-text">${p.hp}/${p.maxHp}</span></span>
    </div>
    <div class="row"><span class="row-label">手牌${isMe ? '' : `(${p.handCount})`}</span><span class="cards">${handHtml}</span></div>
    <div class="row"><span class="row-label">装备</span><span class="cards">${equipHtml}</span></div>
    <div class="row"><span class="row-label">判定区</span><span class="cards">${judgeHtml}</span></div>
  `;
}

// ---------------------------------------------------------------- 日志
function logLine(text) {
  const box = $('log');
  const div = document.createElement('div');
  let cls = '';
  if (/^===/.test(text)) cls = 'l-sys';
  else if (/^=====/.test(text)) cls = 'l-turn';
  else if (/^  ——/.test(text)) cls = 'l-phase';
  else if (/造成|阵亡|濒死/.test(text)) cls = 'l-dmg';
  if (cls) div.className = cls;
  div.textContent = text.replace(/^\s+/, m => m);   // 保留缩进
  box.appendChild(div);
  while (box.childElementCount > 600) box.removeChild(box.firstChild);
  box.scrollTop = box.scrollHeight;
}

// ---------------------------------------------------------------- 决策区
function renderAsk(ask) {
  S.pending = ask;
  S.selected = new Set();
  const bar = $('actionBar');

  const title = `<div class="ask-head">
      <span class="ask-title">${ask.prompt}</span>
      <span class="ask-hint">${hintFor(ask)}</span>
    </div>`;

  let body = '';
  if (ask.kind === 'card' || ask.kind === 'multi') {
    const cards = ask.cards.map((c, i) =>
      `<div class="ask-card" data-card="${c.id}">${c.label.startsWith('手牌(未知)')
        ? '手牌(未知)'
        : c.label}<span class="area">${c.area}</span></div>`).join('');
    body = `<div class="ask-list">${cards}</div>`;
  } else if (ask.kind === 'option') {
    const opts = ask.options.map((o, i) => `<div class="ask-opt" data-opt="${i}">${o}</div>`).join('');
    body = `<div class="ask-list">${opts}</div>`;
  } else if (ask.kind === 'target') {
    const ts = ask.targets.map(t =>
      `<div class="ask-target" data-target="${t.id}">${t.name}（体力 ${t.hp}/${t.maxHp}）</div>`).join('');
    body = `<div class="ask-list">${ts}</div>`;
  }

  bar.innerHTML = title + body + `
    <div class="ask-actions">
      <button class="btn primary" id="askConfirm" ${needConfirm(ask) ? '' : 'style="display:none"'}>确认</button>
      <button class="btn ghost" id="askCancel">${cancelText(ask)}</button>
      <button class="btn ghost" id="askAuto">托管 AI</button>
    </div>`;

  // 事件绑定
  bar.querySelectorAll('[data-card]').forEach(el => el.onclick = () => {
    const id = Number(el.dataset.card);
    if (ask.kind === 'multi') {
      if (S.selected.has(id)) S.selected.delete(id); else S.selected.add(id);
      el.classList.toggle('sel');
      updateConfirmLabel();
    } else {
      submit(-1, [id], []);
    }
  });
  bar.querySelectorAll('[data-opt]').forEach(el => el.onclick = () => submit(Number(el.dataset.opt), [], []));
  bar.querySelectorAll('[data-target]').forEach(el => el.onclick = () => {
    const id = Number(el.dataset.target);
    if (ask.maxTargets > 1) {
      if (S.selected.has(id)) S.selected.delete(id); else S.selected.add(id);
      el.classList.toggle('sel');
      updateConfirmLabel();
    } else {
      submit(-1, [], [id]);
    }
  });
  const confirm = $('askConfirm');
  if (confirm) confirm.onclick = () => {
    if (ask.kind === 'multi') submit(-1, [...S.selected], []);
    else submit(-1, [], [...S.selected]);
  };
  $('askCancel').onclick = () => submit(-1, [], []);
  $('askAuto').onclick = () => {
    window.sgsAutoPlay = true;
    logLine('  [托管] 交给 AI 继续');
    submit(-1, [], []);
  };
  updateConfirmLabel();
}

function needConfirm(ask) {
  if (ask.kind === 'multi') return ask.maxCards > 1 || ask.minCards > 1;
  if (ask.kind === 'target') return ask.maxTargets > 1;
  return false;
}

function cancelText(ask) {
  if (ask.kind === 'option') return '结束出牌阶段 / 放弃';
  return '放弃';
}

function hintFor(ask) {
  if (ask.kind === 'multi') return `需要选 ${ask.minCards} 张`;
  if (ask.kind === 'target') return ask.minTargets === ask.maxTargets
    ? `选择 ${ask.minTargets} 个目标` : `选择 ${ask.minTargets}-${ask.maxTargets} 个目标`;
  if (ask.kind === 'option') return '点击一项执行，或选择放弃';
  return '点击一张牌打出';
}

function updateConfirmLabel() {
  const btn = $('askConfirm');
  if (!btn) return;
  const need = S.pending ? (S.pending.kind === 'multi' ? S.pending.minCards : S.pending.minTargets) : 1;
  btn.textContent = `确认（已选 ${S.selected.size}/${need}）`;
}

function submit(option, cards, targets) {
  window.sgsAnswer = { ready: true, option: option | 0, cards: cards || [], targets: targets || [] };
  S.pending = null;
  S.selected = new Set();
  $('actionBar').innerHTML = '<div class="waiting">结算中…</div>';
}

function showResult(winner) {
  const old = document.querySelector('.result-banner');
  if (old) old.remove();
  let text;
  if (winner < 0) text = '平局';
  else if (S.humanSeat < 0) text = `${S.players[winner] ? S.players[winner].name : '?'} 获胜`;
  else text = winner === S.humanSeat ? '🎉 你赢了！' : '你输了…';
  const div = document.createElement('div');
  div.className = 'result-banner';
  div.textContent = text;
  document.body.appendChild(div);
  setTimeout(() => div.remove(), 6000);
}

// ---------------------------------------------------------------- C++ 回调
window.sgsOnLog = (text) => logLine(text);

window.sgsOnState = (json) => {
  try {
    const st = JSON.parse(json);
    S.players = st.players;
    S.current = st.current;
    S.turn = st.turn;
    S.deck = st.deck;
    S.discard = st.discard;
    S.over = st.over;
    S.winner = st.winner;
    renderBoard();
  } catch (e) { console.error('state parse', e, json); }
};

window.sgsOnAsk = (json) => {
  try {
    renderAsk(JSON.parse(json));
  } catch (e) { console.error('ask parse', e, json); }
};

window.sgsOnDone = () => {
  S.started = false;
  S.over = true;
  $('restart').disabled = false;
  $('actionBar').innerHTML = '<div class="waiting">本局结束，可以重新开始。</div>';
  showResult(S.winner);
};

// ---------------------------------------------------------------- 开局
function startGame(humanSeat) {
  if (S.started) return;
  S.started = true;
  S.humanSeat = humanSeat;
  S.over = false;
  S.winner = -1;
  window.sgsAutoPlay = false;
  window.sgsAnswer = { ready: false, option: -1, cards: [], targets: [] };
  $('log').innerHTML = '';
  $('cover').classList.add('hide');
  $('restart').disabled = true;
  $('actionBar').innerHTML = '<div class="waiting">开局中…</div>';

  const seed = parseInt($('seedInput').value || '1', 10) || 1;
  // 这一句会一直跑到本局结束（需要玩家决策时会挂起等页面）
  S.module._sgs_start(seed, humanSeat, 4);
}

// 随机种子：每次刷新换一个，免得每局牌堆都一样；想复现就把它抄下来
function randomSeed() {
  return Math.floor(Math.random() * 2147483646) + 1;
}

function setRandomSeed() {
  const box = $('seedInput');
  if (box) box.value = String(randomSeed());
}

function bindUi() {
  document.querySelectorAll('[data-start]').forEach(btn => {
    btn.onclick = () => startGame(Number(btn.dataset.start));
  });
  setRandomSeed();
  const reroll = $('reseed');
  if (reroll) reroll.onclick = setRandomSeed;
  $('speed').oninput = (e) => {
    window.sgsSpeedMs = Number(e.target.value);
    $('speedVal').textContent = `${e.target.value}ms`;
  };
  $('clearLog').onclick = () => { $('log').innerHTML = ''; };
  $('restart').onclick = () => {
    setRandomSeed();            // 重开一局也换新种子，避免重复上一局
    $('cover').classList.remove('hide');
    $('loadState').textContent = '选择一种开局方式…';
  };
  window.sgsSpeedMs = Number($('speed').value);
  window.sgsAnswer = { ready: false, option: -1, cards: [], targets: [] };
  window.sgsAutoPlay = false;
}

function boot() {
  bindUi();
  if (typeof createSgsModule !== 'function') {
    $('loadState').textContent = '找不到 sgs.js —— 请先用 Emscripten 编译（见 web/build_web.ps1）';
    return;
  }
  createSgsModule().then((m) => {
    S.module = m;
    $('loadState').textContent = 'wasm 已就绪（C++ 规则内核跑在浏览器里）';
  }).catch((e) => {
    $('loadState').textContent = 'wasm 加载失败：' + e;
  });
}

document.addEventListener('DOMContentLoaded', boot);
