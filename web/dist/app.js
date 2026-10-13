/* 场景与操作层。只写 sgsAnswer / 设置变量；Asyncify 挂起时不重入 wasm。 */
'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const { escape: esc, face, rules } = window.SgsCards;
  const I = window.SgsInteraction;
  const motion = window.SgsMotion;
  const S = { module: null, running: false, seat: 0, state: null, model: null,
    focusCard: null, cards: new Map(), trail: [], logs: [], seed: 1, audio: null,
    handPage: 0, logHover: false, logPinned: false, logFocus: false, logUnread: 0,
    infoCard: null, origins: new Map(), activeGroup: 0, trailTimer: null, trailVersion: 0, latestJudge: -1 };
  const dialogIds = ['menuDialog', 'historyDialog', 'resultDialog'];
  const settingsKey = 'simplesgs.ui.v2';
  const nameOf = id => S.state?.players.find(p => p.id === id)?.name || '对手';
  const bottomId = () => S.seat === 1 ? 1 : 0;
  const ownHand = () => S.state?.players.find(p => p.id === bottomId())?.hand || [];
  const randomSeed = () => Math.floor(Math.random() * 2147483646) + 1;

  function replace(el, html) {
    if (el.innerHTML === html) return;
    const active = el.contains(document.activeElement) ? document.activeElement : null;
    const key = active?.dataset.focusKey;
    el.innerHTML = html;
    if (key) [...el.querySelectorAll('[data-focus-key]')].find(e => e.dataset.focusKey === key)?.focus({ preventScroll: true });
  }
  function showDialog(id) {
    dialogIds.forEach(other => { if (other !== id && $(other).open) $(other).close(); });
    if (!$(id).open) $(id).showModal();
  }
  function closeDialogs() { dialogIds.forEach(id => { if ($(id).open) $(id).close(); }); }
  function rememberCards(cards) { cards.forEach(c => { if (!c.hidden) S.cards.set(c.id, c); }); }
  function sound(kind) {
    if (!$('soundSetting').checked || !S.audio) return;
    const ctx = S.audio;
    if (ctx.state === 'suspended') return;
    const osc = ctx.createOscillator(), gain = ctx.createGain();
    osc.type = 'triangle'; osc.frequency.setValueAtTime(kind === 'damage' ? 100 : 330, ctx.currentTime);
    osc.frequency.exponentialRampToValueAtTime(65, ctx.currentTime + .075);
    gain.gain.setValueAtTime(.045, ctx.currentTime); gain.gain.exponentialRampToValueAtTime(.001, ctx.currentTime + .09);
    osc.connect(gain); gain.connect(ctx.destination); osc.start(); osc.stop(ctx.currentTime + .1);
  }
  function enableAudio() {
    if (!$('soundSetting').checked) return;
    try {
      const Audio = window.AudioContext || window.webkitAudioContext;
      if (Audio) { S.audio ||= new Audio(); S.audio.resume().catch(() => {}); }
    } catch { /* 不支持音频时仍可正常对局。 */ }
  }
  function saveSettings() {
    try { localStorage.setItem(settingsKey, JSON.stringify({ bagua: $('baguaSelect').value,
      speed: $('speedSelect').value, sound: $('soundSetting').checked, motion: $('motionSetting').checked })); } catch { /* 私密模式 */ }
  }
  function applySettings() {
    window.sgsBaguaMode = Number($('baguaSelect').value);
    window.sgsSpeedMs = Number($('speedSelect').value);
    document.documentElement.classList.toggle('reduce-motion', $('motionSetting').checked);
    if (!motion.enabled()) motion.clear();
    saveSettings();
  }
  function loadSettings() {
    $('motionSetting').checked = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches || false;
    try {
      const saved = JSON.parse(localStorage.getItem(settingsKey) || '{}');
      if (['0','1','2'].includes(saved.bagua)) $('baguaSelect').value = saved.bagua;
      if (['0','100','320','550'].includes(saved.speed)) $('speedSelect').value = saved.speed;
      if (typeof saved.sound === 'boolean') $('soundSetting').checked = saved.sound;
      if (typeof saved.motion === 'boolean') $('motionSetting').checked = saved.motion;
    } catch { /* 损坏的设置使用默认值。 */ }
    applySettings();
  }
  function logLine(text) {
    if (!text.trim()) return;
    S.logs.push(text);
    const lines = $('battleLogLines');
    const follow = !logReading() || lines.scrollHeight - lines.scrollTop - lines.clientHeight < 12;
    const line = document.createElement('p');
    line.textContent = text;
    lines.append(line);
    if (follow) lines.scrollTop = lines.scrollHeight;
    else S.logUnread++;
    updateLogLatest();
    if ($('historyDialog').open) renderHistory();
  }
  function logReading() { return S.logHover || S.logPinned || S.logFocus; }
  function updateLogLatest() {
    $('logLatest').hidden = !logReading() || !S.logUnread;
    $('logLatest').textContent = `回到最新 · ${S.logUnread} 条新记录`;
  }
  function updateLogReading() {
    const el = $('battleLog'), lines = $('battleLogLines');
    const wasReading = el.classList.contains('reading');
    const reading = logReading();
    el.classList.toggle('reading', reading);
    $('logToggle').setAttribute('aria-expanded', String(reading));
    $('logToggleHint').textContent = reading ? '收起' : '展开';
    // 高度切换后仍从最新记录开始；正在回看时则保持原位置。
    if (!reading || !wasReading) { lines.scrollTop = lines.scrollHeight; S.logUnread = 0; }
    updateLogLatest();
  }
  function closeLog() {
    S.logPinned = S.logHover = S.logFocus = false;
    if ($('battleLog').contains(document.activeElement)) document.activeElement.blur();
    updateLogReading();
  }
  function renderHistory() {
    $('historySummary').textContent = S.state ? `第 ${S.state.turn} 回合 · 种子 ${S.seed}` : '尚未开始对局';
    const nearBottom = $('log').scrollHeight - $('log').scrollTop - $('log').clientHeight < 50;
    replace($('log'), S.logs.map(line => `<p>${esc(line)}</p>`).join('') || '<p>暂无记录</p>');
    if (nearBottom) $('log').scrollTop = $('log').scrollHeight;
  }
  function history() { renderHistory(); showDialog('historyDialog'); $('log').scrollTop = $('log').scrollHeight; }

  function selectedCard(id) {
    const m = S.model;
    return !!m && (m.cards.has(id) || (m.option >= 0 && m.ask.optionCards[m.option] === id));
  }
  function legalCard(id) {
    const a = S.model?.ask;
    return !!a && !window.sgsAutoPlay && (a.cards.some(c => c.id === id) || (a.tag === 'Play' && a.optionCards.includes(id)));
  }
  function targetState(id) {
    const m = S.model;
    if (!m || window.sgsAutoPlay) return '';
    if (m.ask.kind === 'target' && m.ask.targets.some(t => t.id === id)) return m.targets.has(id) ? ' target selected-target' : ' target';
    if (m.option >= 0 && m.ask.optionDetails?.[m.option]?.targets.includes(id)) return ' preview-target';
    return '';
  }
  function equipmentAction(card, owner) {
    const a = S.model?.ask;
    if (!a || window.sgsAutoPlay) return null;
    if (a.cards.some(c => c.id === card.id)) return { kind: 'card' };
    if (owner !== S.seat) return null;
    if (card.name === 'Spear' && a.tag === 'Play') {
      const option = a.optionCards.findIndex((id, i) => id < 0 && a.optionDetails[i]?.card?.name === 'Slash');
      if (option >= 0) return { kind: 'skill', option };
    }
    return null;
  }
  function renderZoneCard(card, owner, judge = false) {
    const action = equipmentAction(card, owner), selected = selectedCard(card.id);
    const tag = action ? 'button' : 'div';
    const label = action?.kind === 'skill' ? `发动${card.display}` : action ? `选择${card.display}` : card.display;
    const hook = action?.kind === 'skill' ? `data-skill="${action.option}"` : action ? `data-card="${card.id}" aria-pressed="${selected}"` : 'tabindex="0"';
    return `<${tag} class="${judge ? 'judge-item' : 'equip-item'} ${action ? 'actionable' : ''} ${selected ? 'selected' : ''}" ${hook} data-help="${card.id}" data-card-id="${card.id}" data-focus-key="zone-${card.id}" aria-label="${esc(label)}" aria-describedby="handDetailText">${judge ? '' : `<span class="equip-art" style="${window.SgsCards.art(card)}"></span>`}<span>${esc(card.display)}</span>${action?.kind === 'skill' ? '<span class="equip-action">发动</span>' : ''}</${tag}>`;
  }
  function renderSeat(id, player) {
    const active = S.state.current === player.id && !S.state.over;
    const target = targetState(player.id);
    const status = !player.alive ? '已阵亡' : player.hp <= 0 ? '濒死' : active ? S.state.phase : '';
    const canTarget = S.model?.ask.kind === 'target' && target.includes('target') && !window.sgsAutoPlay;
    const hp = Array.from({ length: player.maxHp }, (_, i) => `<span class="hp-segment ${i < player.hp ? '' : 'empty'}"></span>`).join('');
    const equip = player.equip.map(c => renderZoneCard(c, player.id)).join('');
    const judges = player.judge.map(c => renderZoneCard(c, player.id, true)).join('');
    replace($(id), `<div class="seat-person ${active ? 'active' : ''}${target} ${player.alive ? '' : 'dead'}">
      <button class="portrait portrait-${player.id}" data-target="${player.id}" data-focus-key="target-${player.id}" aria-label="${canTarget ? '选择目标：' : '查看状态：'}${esc(player.name)}" ${canTarget ? `aria-pressed="${S.model.targets.has(player.id)}"` : ''}><span class="portrait-shade"></span><span class="seat-name">${esc(player.name)}</span></button>
      <div class="health" aria-label="体力 ${player.hp} / ${player.maxHp}">${hp}<span class="hp-number">${player.hp}/${player.maxHp}</span></div>
      <div class="seat-meta"><span>手牌 ${player.handCount}</span>${window.sgsAutoPlay && player.id === S.seat ? '<span>托管中</span>' : ''}</div>
      <div class="seat-phase">${esc(status)}</div>
    </div><div class="seat-items">${equip}${judges ? `<div class="judge-items">${judges}</div>` : ''}<span class="seat-distance">范围 ${player.range} · 距离 ${player.distance}</span></div>`);
  }
  function renderHand() {
    const cards = ownHand();
    const cardHtml = c => {
      const allowed = legalCard(c.id), selected = selectedCard(c.id), focused = S.focusCard?.id === c.id;
      return `<button class="hand-card ${allowed ? 'available' : 'unavailable'} ${selected ? 'selected' : ''} ${focused ? 'inspected' : ''}" data-hand="${c.id}" data-help="${c.id}" data-card-id="${c.id}" data-focus-key="hand-${c.id}" aria-pressed="${selected}" aria-label="${esc(c.label)}${allowed ? '，可选择' : ''}" aria-describedby="handDetailText">${face(c)}</button>`;
    };
    if (cards.length && !$('hand').querySelector('.hand-card')) replace($('hand'), cardHtml(cards[0]));
    const width = $('hand').clientWidth;
    const cardWidth = $('hand').querySelector('.hand-card')?.getBoundingClientRect().width || 108;
    const capacity = Math.max(1, Math.floor((width - cardWidth - 16) / 44) + 1);
    const pages = Math.max(1, Math.ceil(cards.length / capacity));
    S.handPage = Math.min(S.handPage, pages - 1);
    const shown = cards.slice(S.handPage * capacity, (S.handPage + 1) * capacity);
    replace($('hand'), shown.map(cardHtml).join('') || '<p class="empty-hand">暂无手牌</p>');
    // 每张牌至少露出 44px；过多时翻页，选择状态在各页间保留。
    const step = Math.max(44, Math.min(cardWidth + 8, (width - cardWidth - 16) / Math.max(1, shown.length - 1)));
    $('hand').style.setProperty('--card-step', `${step}px`);
    $('hand').classList.toggle('crowded', step < cardWidth * .78);
    $('handPages').hidden = pages === 1;
    $('handPrev').disabled = S.handPage === 0;
    $('handNext').disabled = S.handPage === pages - 1;
    $('handPageNumber').textContent = `${S.handPage + 1}/${pages}`;
    $('handPageNumber').setAttribute('aria-label', `手牌第 ${S.handPage + 1} 组，共 ${pages} 组`);
    renderInfo();
  }
  function renderInfo() {
    const c = S.infoCard || S.focusCard;
    $('handDetail').hidden = !c;
    $('handDetailText').textContent = c ? `${c.label || c.display} · ${rules[c.name] || c.type || ''}` : '';
  }
  function cardHelp(event) {
    const el = event.target.closest('[data-help]');
    if (!el) return;
    const card = S.cards.get(Number(el.dataset.help));
    if (card && !card.hidden) { S.infoCard = card; renderInfo(); }
  }
  function renderChoices() {
    const m = S.model, a = m?.ask;
    if (!a || window.sgsAutoPlay) { $('choicePanel').hidden = true; replace($('extraOptions'), ''); return; }
    const handIds = new Set(ownHand().map(c => c.id));
    const boardIds = new Set(S.state?.players.flatMap(p => [...p.equip, ...p.judge].map(c => c.id)) || []);
    const outside = a.cards.filter(c => !handIds.has(c.id) && !boardIds.has(c.id));
    let choices = outside.map(c => `<button class="choice-card ${m.cards.has(c.id) ? 'selected' : ''}" data-card="${c.id}" ${c.hidden ? '' : `data-help="${c.id}"`} data-card-id="${c.id}" data-focus-key="choice-${c.id}" aria-pressed="${m.cards.has(c.id)}" aria-label="${esc(c.label)}，${esc(c.area)}">${face(c)}<span class="choice-area">${esc(c.area)}</span></button>`).join('');
    let extras = '';
    a.options.forEach((label, index) => {
      const id = a.optionCards[index];
      if (a.tag === 'Play' && id >= 0 && handIds.has(id)) return;
      if (a.tag === 'EightDiagram') {
        if (index === 0) extras += '<button class="primary" data-skill="0" data-focus-key="bagua-activate">发动八卦阵</button>';
        return;
      }
      if (a.tag === 'Play' && id < 0 && S.state?.players.find(p => p.id === S.seat)?.equip.some(c => equipmentAction(c, S.seat)?.option === index)) return;
      const c = a.optionDetails?.[index]?.card;
      if (c && a.tag !== 'Play') choices += `<button class="choice-card ${m.option === index ? 'selected' : ''}" data-option="${index}" data-help="${c.id}" data-card-id="${c.id}" data-focus-key="option-${index}" aria-pressed="${m.option === index}" aria-label="${esc(label)}">${face(c)}</button>`;
      else {
        const button = `<button class="secondary ${m.option === index ? 'chosen' : ''}" data-option="${index}" data-focus-key="option-${index}" aria-pressed="${m.option === index}" aria-label="${esc(label)}">${esc(a.tag === 'Play' ? '丈八蛇矛' : label)}</button>`;
        extras += button;
      }
    });
    const revealed = !choices && S.state?.revealed?.length;
    if (revealed) choices = S.state.revealed.map(c => `<div class="choice-card" data-help="${c.id}" data-card-id="${c.id}" tabindex="0">${face(c)}</div>`).join('');
    $('choicePanel').hidden = !choices;
    $('choiceHeading').textContent = revealed ? '五谷丰登 · 已亮出' : a.tag === 'Harvest' ? '选择一张获得' : a.kind === 'option' ? '选择行动' : a.subject >= 0 ? `${nameOf(a.subject)}的牌` : '选择牌';
    replace($('choiceCards'), choices); replace($('extraOptions'), extras);
  }
  function renderDecision() {
    const m = S.model, a = m?.ask, auto = window.sgsAutoPlay;
    $('takeControl').hidden = !(S.running && S.seat >= 0 && auto);
    $('confirmButton').hidden = !a || auto || a.tag === 'EightDiagram';
    $('cancelButton').hidden = !a || auto || a.allowCancel === false;
    $('confirmButton').disabled = !I.valid(m);
    $('selectionCount').textContent = '';
    if (!a || auto) {
      $('prompt').textContent = !S.running ? '对局结束' : auto ? 'AI 正在托管' : S.seat < 0 ? '观战中' : S.state?.current === S.seat ? '结算中' : '等待对手行动';
      return;
    }
    const selected = m.option >= 0 ? a.optionDetails?.[m.option]?.card : a.cards.find(c => m.cards.has(c.id));
    let prompt = a.prompt;
    if (a.tag === 'Play') prompt = m.option >= 0 ? `使用${selected?.display || '丈八蛇矛'}${a.optionDetails?.[m.option]?.targets.length ? ` → ${a.optionDetails[m.option].targets.map(nameOf).join('、')}` : ''}` : '请选择要使用的手牌';
    if (a.tag === 'Discard') prompt = `弃置 ${I.bounds(m)[0]} 张手牌`;
    if (a.tag === 'ViewAs') prompt = '选择两张手牌，当作杀使用';
    if (a.tag === 'AxeCost') prompt = '贯石斧：弃置两张牌，强制命中';
    if (a.tag === 'EightDiagram') prompt = '需要闪，是否发动八卦阵判定？';
    $('prompt').textContent = prompt;
    if (a.kind === 'multi' || a.kind === 'target') {
      const target = a.kind === 'target', [min,max] = I.bounds(m,target);
      $('selectionCount').textContent = `已选 ${target ? m.targets.size : m.cards.size} / ${min === max ? min : `${min}–${max}`}`;
    }
    const response = ['Jink','Slash','Peach','Nullification'].includes(a.tag);
    $('confirmButton').textContent = a.tag === 'Play' ? '使用' : a.tag === 'Discard' ? '确认弃牌' : a.tag === 'Harvest' ? '获得此牌' : response && selected ? `打出${selected.display}` : '确认';
    $('cancelButton').textContent = a.tag === 'Play' ? '结束出牌' : a.kind === 'target' || a.tag === 'ViewAs' ? '取消使用' : a.tag === 'EightDiagram' ? '不发动' : '不响应';
  }
  function renderBoard() {
    if (S.state?.players.length === 2) {
      renderSeat('selfSeat', S.state.players.find(p => p.id === bottomId()));
      renderSeat('opponentSeat', S.state.players.find(p => p.id !== bottomId()));
      $('deckCount').textContent = S.state.deck; $('discardCount').textContent = S.state.discard;
      replace($('discardTop'), S.state.discardTop ? face(S.state.discardTop) : '');
    }
    renderHand(); renderDecision(); renderChoices();
    $('autoButton').hidden = !S.running || S.seat < 0;
    $('returnLobbyButton').hidden = S.running || !S.state || !$('lobby').hidden;
    $('autoButton').textContent = window.sgsAutoPlay ? '接管对局' : '托管给 AI';
  }
  function renderTrail() {
    clearTimeout(S.trailTimer);
    S.trailVersion++;
    $('playedCards').classList.remove('settling');
    replace($('playedCards'), S.trail.map((entry, i) => `<div class="table-card ${i === S.trail.length - 1 ? 'latest' : 'previous'}" data-help="${entry.card.id}" data-card-id="${entry.card.id}" tabindex="0"><span class="play-owner">${esc(entry.label)}</span>${face(entry.card)}</div>`).join(''));
  }
  function settleTrail(group, delay = 180) {
    if (group !== S.activeGroup) return;
    clearTimeout(S.trailTimer);
    const version = S.trailVersion;
    S.trailTimer = setTimeout(() => {
      if (version !== S.trailVersion) return;
      $('playedCards').classList.add('settling');
      $('eventCaption').textContent = '';
      S.trailTimer = setTimeout(() => {
        if (version !== S.trailVersion) return;
        S.trail = []; renderTrail();
      }, motion.enabled() ? 220 : 0);
    }, motion.enabled() ? delay : 0);
  }
  function submit(cancel = false) {
    if (window.sgsAutoPlay) return;
    const answer = I.answer(S.model, cancel);
    if (!answer) return;
    // 先撤销 UI 的询问，再发布答案；连点不能重复提交。
    S.model = null; S.focusCard = S.infoCard = null;
    window.sgsAnswer = answer;
    renderBoard();
  }
  function setAuto(value) {
    window.sgsAutoPlay = value;
    // 保留尚未被 wasm 消费的询问；快速托管/接管也不会丢失它。
    renderBoard(); closeDialogs();
    $('announcement').textContent = value ? '已托管给 AI' : '已接管，将在下一次决策时操作';
  }
  function onBoardClick(event) {
    const button = event.target.closest('button');
    if (!button) return;
    if (button.dataset.skill !== undefined && S.model && !window.sgsAutoPlay) {
      I.choose(S.model, Number(button.dataset.skill)); submit(); return;
    }
    if (button.dataset.target !== undefined) {
      const id = Number(button.dataset.target);
      if (S.model?.ask.kind === 'target' && !window.sgsAutoPlay) { I.toggle(S.model,id,true); renderBoard(); }
      else { const p = S.state.players.find(p => p.id === id); $('announcement').textContent = `${p.name}，体力 ${p.hp}/${p.maxHp}，手牌 ${p.handCount}，攻击范围 ${p.range}，距离 ${p.distance}`; }
      return;
    }
    if (button.dataset.hand !== undefined) {
      const id = Number(button.dataset.hand);
      S.focusCard = S.cards.get(id);
      if (S.model && !window.sgsAutoPlay) {
        const a = S.model.ask;
        if (a.tag === 'Play') { const index = a.optionCards.indexOf(id); if (index >= 0) I.choose(S.model,index); else S.model = I.create(a); }
        else I.toggle(S.model,id);
      }
      renderBoard(); return;
    }
    if (!S.model || window.sgsAutoPlay) return;
    if (button.dataset.card !== undefined) I.toggle(S.model,Number(button.dataset.card));
    if (button.dataset.option !== undefined) I.choose(S.model,Number(button.dataset.option));
    renderBoard();
  }

  window.sgsOnLog = logLine;
  window.sgsOnState = json => {
    const state = JSON.parse(json);
    S.origins = motion.capture();
    S.state = state;
    state.players.forEach(p => { rememberCards(p.hand); rememberCards(p.equip); rememberCards(p.judge); });
    rememberCards(state.revealed || []);
    renderBoard();
  };
  window.sgsOnAsk = json => {
    const ask = JSON.parse(json);
    S.model = I.create(ask); S.focusCard = S.infoCard = null;
    rememberCards(ask.cards); (ask.optionDetails || []).forEach(o => { if (o.card) rememberCards([o.card]); });
    renderBoard();
  };
  window.sgsOnAskClosed = requestId => {
    if (S.model?.ask.requestId === requestId) { S.model = null; renderBoard(); }
  };
  window.sgsOnEvent = json => {
    const event = JSON.parse(json), who = nameOf(event.who);
    if (event.moves) {
      const discarded = event.areaIndex === 5 ? event.moves.filter(m => m.fromArea !== 0) : [];
      if (discarded.length) {
        if (S.activeGroup !== event.group) S.trail = [];
        S.activeGroup = event.group;
        discarded.forEach(m => { rememberCards([m.card]); S.trail.push({ card: m.card, who: m.fromOwner, label: `${m.fromOwner < 0 ? '公共牌' : nameOf(m.fromOwner)} · 弃置` }); });
        S.trail = S.trail.slice(-4); renderTrail();
        $('eventCaption').textContent = `弃置 ${discarded.length} 张牌`;
        if (!event.group) settleTrail(0, 650);
      }
      event.moves.forEach((move, index) => {
        const from = S.origins.get(move.card.id) || motion.region(move.fromOwner, move.fromArea, bottomId());
        if (event.areaIndex === 5) {
          // 弃置进入中央结算牌列；已结算的牌只淡出，不飞向弃牌堆。
          if (move.fromArea !== 0) {
            const landing = $('playedCards').querySelector(`[data-card-id="${move.card.id}"] .playing-card`);
            if (landing) motion.fly(move.card, from, landing.getBoundingClientRect(), { landing, delay: Math.min(index, 3) * 30 });
          } else if (!event.group && move.card.id === S.latestJudge) settleTrail(0, 600);
        } else if (event.areaIndex === 1 && move.fromArea !== 2 && move.fromArea !== 3) {
          motion.draw(move.card, event.who, bottomId(), Math.min(index, 3) * 45);
        } else if (event.areaIndex === 6) {
          // 公共亮牌在中央出现，后续选择仍由询问控制。
          motion.reveal(move.card, motion.region(-1, 6, bottomId(), move.card.id));
        } else {
          const to = motion.region(event.who, event.areaIndex, bottomId(), move.card.id);
          motion.fly(move.card, from, to, { delay: Math.min(index, 3) * 35 });
        }
      });
    }
    if (event.card) {
      rememberCards([event.card]);
      if (event.type === 'use' || (!event.group && S.activeGroup !== 0)) S.trail = [];
      S.activeGroup = event.group;
      const label = event.type === 'judge' ? `${who} · ${event.reason}判定` : event.type === 'response' ? `${who} · ${event.reason || '响应'}` : who;
      S.trail.push({ card: event.card, who: event.who, label });
      S.trail = S.trail.slice(-4); renderTrail();
      const landing = $('playedCards').lastElementChild?.querySelector('.playing-card');
      if (event.type === 'judge') {
        S.latestJudge = event.card.id;
        const version = S.trailVersion;
        $('eventCaption').textContent = `${event.reason} · 判定中`;
        motion.judge(event.card, landing).then(() => {
          if (S.trailVersion === version) $('eventCaption').textContent = `${event.reason} · ${event.card.suit}${event.card.rank} · ${event.card.display}`;
        });
      } else {
        const from = S.origins.get(event.card.id) || motion.region(event.who, 1, bottomId());
        if (landing) motion.fly(event.card, from, landing.getBoundingClientRect(), { landing });
        $('eventCaption').textContent = `${who}${event.type === 'response' ? '打出' : '使用'}${event.card.display}`;
        if (event.type === 'use') {
          S.latestJudge = -1;
          motion.targets(landing, event.who, event.targets || [], bottomId());
        }
      }
      sound('card');
      if (event.type === 'response' && !event.group) settleTrail(0, 650);
    }
    if (event.type === 'finish') settleTrail(event.group);
    if (['damage','recover','dying'].includes(event.type)) {
      $('eventCaption').textContent = event.type === 'damage' ? `${who}受到 ${event.amount} 点伤害` : event.type === 'recover' ? `${who}回复 ${event.amount} 点体力` : `${who}濒死，等待救援`;
      const seat = $(event.who === bottomId() ? 'selfSeat' : 'opponentSeat');
      if (motion.enabled() && seat.animate) seat.animate(event.type === 'damage' ? [{transform:'translateX(0)'},{transform:'translateX(-5px)'},{transform:'translateX(4px)'},{transform:'translateX(0)'}] : [{filter:'brightness(1)'},{filter:'brightness(1.3)'},{filter:'brightness(1)'}], {duration:220});
      sound(event.type);
    }
  };
  window.sgsOnDone = () => {
    S.running = false; S.model = null; window.sgsAutoPlay = false;
    renderBoard();
    const st = S.state;
    $('resultTitle').textContent = !st?.over ? '对局未能开始' : st.winner < 0 ? '平局' : S.seat < 0 ? `${nameOf(st.winner)}获胜` : st.winner === S.seat ? '胜利' : '败北';
    $('resultSummary').textContent = st ? `共 ${st.turn} 回合` : '请在战报中查看原因。';
    $('seedInput').disabled = false; $('reseed').disabled = false;
    showDialog('resultDialog');
  };
  function startGame(seat) {
    if (S.running || !S.module) return;
    const input = Number($('seedInput').value);
    if (!Number.isInteger(input) || input < 1 || input > 2147483646) {
      showDialog('menuDialog'); document.querySelector('.replay-settings').open = true;
      $('seedInput').focus(); $('seedInput').reportValidity(); return;
    }
    closeDialogs(); enableAudio();
    S.running = true; S.seat = seat; S.seed = input; S.state = null; S.model = null;
    S.focusCard = S.infoCard = null; S.trail = []; S.logs = []; S.cards.clear(); motion.clear();
    S.activeGroup = 0; S.latestJudge = -1; clearTimeout(S.trailTimer);
    S.handPage = 0; S.logUnread = 0; $('battleLogLines').replaceChildren(); closeLog();
    window.sgsAutoPlay = false;
    window.sgsAnswer = { ready: false, requestId: -1, option: -1, cards: [], targets: [] };
    $('lobby').hidden = true; $('board').hidden = false; $('game').classList.remove('is-lobby');
    window.scrollTo(0, 0);
    $('seedInput').disabled = true; $('reseed').disabled = true;
    $('eventCaption').textContent = ''; $('discardTop').innerHTML = ''; renderTrail(); renderBoard();
    try { S.module._sgs_start(S.seed, seat, 4, window.sgsBaguaMode); }
    catch (error) { fail(error); }
  }
  function returnToLobby() {
    if (S.running) return;
    closeDialogs(); $('lobby').hidden = false; $('board').hidden = true;
    $('game').classList.add('is-lobby'); $('seedInput').value = randomSeed(); $('startButton').focus();
    $('returnLobbyButton').hidden = true;
    window.scrollTo(0, 0);
  }
  function fail(error) {
    console.error(error);
    S.running = false; S.module = null; S.model = null;
    closeDialogs(); $('lobby').hidden = false; $('board').hidden = true; $('game').classList.add('is-lobby');
    $('loadState').textContent = '牌桌加载失败，请重新加载。'; $('retryLoad').hidden = false;
    $('startButton').disabled = $('spectateButton').disabled = true;
  }
  function bindUi() {
    loadSettings(); $('seedInput').value = randomSeed();
    window.sgsAutoPlay = false; window.sgsAnswer = { ready:false, requestId:-1, option:-1, cards:[], targets:[] };
    $('startButton').onclick = () => startGame(Number(document.querySelector('input[name="seat"]:checked').value));
    $('spectateButton').onclick = () => startGame(-1);
    $('menuButton').onclick = () => showDialog('menuDialog');
    document.querySelectorAll('[data-close]').forEach(b => { b.onclick = () => b.closest('dialog').close(); });
    dialogIds.forEach(id => { $(id).addEventListener('click', e => { if (e.target === $(id)) { const r = $(id).getBoundingClientRect(); if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) $(id).close(); } }); });
    $('historyButton').onclick = $('discardButton').onclick = $('resultHistory').onclick = history;
    $('confirmButton').onclick = () => submit(); $('cancelButton').onclick = () => submit(true);
    $('autoButton').onclick = () => setAuto(!window.sgsAutoPlay); $('takeControl').onclick = () => setAuto(false);
    $('handPrev').onclick = () => { S.handPage--; renderHand(); };
    $('handNext').onclick = () => { S.handPage++; renderHand(); };
    $('battleLog').addEventListener('pointerenter', e => { if (e.pointerType === 'mouse') { S.logHover = true; updateLogReading(); } });
    $('battleLog').addEventListener('pointerleave', e => { if (e.pointerType === 'mouse') { S.logHover = false; updateLogReading(); } });
    $('battleLog').addEventListener('focusin', e => { if (e.target !== $('logToggle')) { S.logFocus = true; updateLogReading(); } });
    $('battleLog').addEventListener('focusout', e => { if (!$('battleLog').contains(e.relatedTarget)) { S.logFocus = false; S.logPinned = false; updateLogReading(); } });
    $('logToggle').onclick = () => { S.logPinned = !S.logPinned; S.logHover = S.logFocus = false; updateLogReading(); };
    $('logLatest').onclick = () => { $('battleLogLines').scrollTop = $('battleLogLines').scrollHeight; S.logUnread = 0; updateLogLatest(); $('battleLogLines').focus({ preventScroll: true }); };
    $('battleLogLines').addEventListener('scroll', () => {
      const el = $('battleLogLines');
      if (el.scrollHeight - el.scrollTop - el.clientHeight < 12) { S.logUnread = 0; updateLogLatest(); }
    });
    document.addEventListener('pointerdown', e => { if (!$('battleLog').contains(e.target)) closeLog(); });
    $('board').addEventListener('click', onBoardClick);
    $('board').addEventListener('pointerover', cardHelp);
    $('board').addEventListener('focusin', cardHelp);
    $('board').addEventListener('pointerdown', cardHelp);
    $('board').addEventListener('pointerout', e => { const card = e.target.closest('[data-help]'); if (card && !card.contains(e.relatedTarget)) { S.infoCard = null; renderInfo(); } });
    $('board').addEventListener('focusout', e => { if (!e.relatedTarget?.closest('[data-help]')) { S.infoCard = null; renderInfo(); } });
    $('reseed').onclick = () => { $('seedInput').value = randomSeed(); };
    ['baguaSelect','speedSelect','soundSetting','motionSetting'].forEach(id => { $(id).onchange = () => { applySettings(); enableAudio(); }; });
    $('rematchButton').onclick = () => { $('seedInput').value = randomSeed(); startGame(S.seat); };
    $('returnButton').onclick = returnToLobby;
    $('returnLobbyButton').onclick = returnToLobby;
    $('retryLoad').onclick = () => window.location.reload();
    document.addEventListener('keydown', e => {
      if (dialogIds.some(id => $(id).open) || /INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
      if ($('battleLog').contains(e.target)) { if (e.key === 'Escape') { e.preventDefault(); closeLog(); $('logToggle').focus({ preventScroll: true }); } return; }
      const choice = e.target.closest('[data-hand],[data-card],[data-option],[data-target]');
      if (e.key === 'Enter' && (choice || e.target === document.body) && I.valid(S.model)) { e.preventDefault(); submit(); }
      if (e.key === 'Escape' && S.model) { e.preventDefault(); S.model = I.create(S.model.ask); S.focusCard = null; renderBoard(); }
    });
    window.addEventListener('resize', renderHand);
  }
  function boot() {
    bindUi();
    if (typeof createSgsModule !== 'function') { fail(new Error('未找到 sgs.js')); return; }
    createSgsModule({ onAbort: reason => fail(new Error(String(reason))) }).then(module => {
      S.module = module; $('loadState').textContent = ''; $('startButton').disabled = $('spectateButton').disabled = false;
    }).catch(fail);
  }
  document.addEventListener('DOMContentLoaded', boot);
})();
