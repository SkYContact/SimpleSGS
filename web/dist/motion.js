/* 只呈现引擎已发生的移动，不改变规则、不等待动画完成才允许操作。 */
'use strict';
window.SgsMotion = (() => {
  const flights = new Set();
  const $ = id => document.getElementById(id);
  function rect(el) {
    const r = el?.getBoundingClientRect();
    return r?.width && r.height ? r : null;
  }
  function capture() {
    const out = new Map();
    document.querySelectorAll('[data-card-id]').forEach(el => {
      const r = rect(el.querySelector('.playing-card') || el);
      if (r) out.set(Number(el.dataset.cardId), r);
    });
    return out;
  }
  function region(owner, area, bottom, id = -1) {
    const seat = $(owner === bottom ? 'selfSeat' : 'opponentSeat');
    let el;
    if (area === 1) el = owner === bottom ? $('hand').querySelector(`[data-card-id="${id}"]`) || $('hand') : seat.querySelector('.portrait');
    else if (area === 2 || area === 3) el = seat.querySelector(`.seat-items [data-card-id="${id}"]`) || seat.querySelector('.seat-items');
    else if (area === 4) el = document.querySelector('.deck-back');
    else if (area === 5) el = $('discardTop');
    else if (area === 6) el = $('choiceCards').querySelector(`[data-card-id="${id}"]`) || $('choicePanel');
    return rect(el) || rect($('playedCards')) || rect(document.querySelector('.table-center'));
  }
  function track(animation, ghost, landing = null) {
    const flight = { animation, ghost, landing };
    flights.add(flight);
    const clean = () => {
      if (landing) landing.style.removeProperty('visibility');
      ghost.remove(); flights.delete(flight);
    };
    const finished = animation.finished.then(clean, clean);
    // 快速结算时限制同时存在的装饰节点。
    if (flights.size > 16) flights.values().next().value.animation.cancel();
    return finished;
  }
  function fly(card, from, to, { landing = null, delay = 0 } = {}) {
    if (!from || !to || !Element.prototype.animate) return;
    const layer = $('motionLayer');
    const ghost = document.createElement('div');
    const width = innerWidth < 761 ? 64 : 90, height = width * 1.48;
    ghost.className = 'flying-card';
    ghost.style.setProperty('--card-w', `${width}px`);
    ghost.innerHTML = window.SgsCards.face(card);
    layer.append(ghost);
    const point = (r, scale, angle) => `translate3d(${r.x + r.width / 2 - width / 2}px,${r.y + r.height / 2 - height / 2}px,0) rotate(${angle}deg) scale(${scale})`;
    const startScale = Math.min(1.4, Math.max(.4, Math.min(from.width / width, from.height / height)));
    const endScale = landing ? to.width / width : Math.min(.85, Math.max(.3, Math.min(to.width / width, to.height / height)));
    if (landing) landing.style.visibility = 'hidden';
    const animation = ghost.animate([
      { transform: point(from, startScale, -5), opacity: .5 },
      { opacity: 1, offset: .22 },
      { transform: point(to, endScale, 0), opacity: landing ? 1 : .1 }
    ], { duration: Math.min(320, Math.max(150, window.sgsSpeedMs)), delay, easing: 'cubic-bezier(.2,.65,.25,1)', fill: 'both' });
    return track(animation, ghost, landing);
  }
  function draw(card, owner, bottom, delay = 0) {
    const landing = owner === bottom ? $('hand').querySelector(`[data-card-id="${card.id}"] .playing-card`) : null;
    const to = rect(landing) || region(owner, 1, bottom, card.id);
    if (!to) return;
    // 从收牌区域上方短距离滑入，不与牌堆位置建立视觉联系。
    const from = { x: to.x, y: to.y - Math.min(76, to.height * .65), width: to.width, height: to.height };
    return fly(card, from, to, { landing, delay });
  }
  function reveal(card, to) {
    if (!to) return;
    return fly(card, { x: to.x, y: to.y - 24, width: to.width, height: to.height }, to);
  }
  function judge(card, landing) {
    const at = rect(landing);
    if (!at || !Element.prototype.animate) return Promise.resolve();
    const ghost = document.createElement('div');
    ghost.className = 'judgment-card';
    ghost.style.cssText = `left:${at.x}px;top:${at.y}px;--card-w:${at.width}px`;
    ghost.innerHTML = `<div class="judgment-turn"><span class="playing-card card-back judgment-back"><span>?</span></span><div class="judgment-face">${window.SgsCards.face(card)}</div></div>`;
    $('motionLayer').append(ghost);
    landing.style.visibility = 'hidden';
    const animation = ghost.firstElementChild.animate([
      { transform: 'rotateY(0deg)' },
      { transform: 'rotateY(180deg)' }
    ], { duration: Math.min(460, window.sgsSpeedMs * .78), delay: Math.min(100, window.sgsSpeedMs * .16), easing: 'cubic-bezier(.3,.1,.2,1)', fill: 'both' });
    return track(animation, ghost, landing);
  }
  function targets(source, who, targetIds, bottom) {
    const ids = [...new Set(targetIds)];
    const start = rect(source);
    if (!start || !ids.some(id => id !== who) || !Element.prototype.animate) return;
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.classList.add('target-lines');
    svg.setAttribute('viewBox', `0 0 ${innerWidth} ${innerHeight}`);
    ids.forEach(id => {
      const end = rect($(id === bottom ? 'selfSeat' : 'opponentSeat').querySelector('.portrait'));
      if (!end) return;
      const sx = start.x + start.width / 2, sy = start.y + start.height / 2;
      const ex = end.x + end.width / 2, ey = end.y + end.height / 2;
      const dx = ex - sx, dy = ey - sy, length = Math.hypot(dx, dy);
      if (length < 1) return;
      const ux = dx / length, uy = dy / length;
      const outset = Math.min(start.width / 2 / (Math.abs(ux) || 1e-6), start.height / 2 / (Math.abs(uy) || 1e-6));
      const fx = sx + ux * (outset + 4), fy = sy + uy * (outset + 4);
      const inset = Math.min(end.width / 2 / (Math.abs(ux) || 1e-6), end.height / 2 / (Math.abs(uy) || 1e-6));
      const tx = ex - ux * (inset + 6), ty = ey - uy * (inset + 6);
      const path = document.createElementNS(ns, 'path');
      path.setAttribute('d', `M ${fx} ${fy} L ${tx} ${ty}`);
      path.setAttribute('pathLength', '100');
      svg.append(path);
      path.animate([{ strokeDashoffset: 100 }, { strokeDashoffset: 0 }], { duration: Math.min(280, window.sgsSpeedMs * .45), fill: 'both', easing: 'ease-out' });
      const dot = document.createElementNS(ns, 'circle');
      dot.setAttribute('cx', tx); dot.setAttribute('cy', ty); dot.setAttribute('r', '3');
      svg.append(dot);
    });
    $('motionLayer').append(svg);
    const animation = svg.animate([{ opacity: 0 }, { opacity: .95, offset: .18 }, { opacity: .95, offset: .65 }, { opacity: 0 }], { duration: Math.min(900, window.sgsSpeedMs * 1.2), fill: 'both' });
    return track(animation, svg);
  }
  function clear() {
    flights.forEach(({ animation, ghost, landing }) => {
      animation.cancel(); ghost.remove();
      if (landing) landing.style.removeProperty('visibility');
    });
    flights.clear();
  }
  return { capture, region, fly, draw, reveal, judge, targets, clear };
})();
