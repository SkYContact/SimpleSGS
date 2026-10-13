/* 询问的选择状态。显示顺序不影响实体牌 id 或选项索引。 */
'use strict';
window.SgsInteraction = (() => {
  function create(ask) { return { ask, cards: new Set(), targets: new Set(), option: -1 }; }
  function bounds(model, target = false) {
    const a = model.ask;
    if (!target && a.kind === 'card') return [1, 1];
    return target ? [a.minTargets, a.maxTargets] : [a.minCards, a.maxCards];
  }
  function toggle(model, id, target = false) {
    const list = target ? model.ask.targets : model.ask.cards;
    if (!list.some(item => item.id === id)) return false;
    const set = target ? model.targets : model.cards;
    const max = bounds(model, target)[1];
    if (set.has(id)) set.delete(id);
    else { if (max === 1) set.clear(); if (max <= 0 || set.size < max) set.add(id); }
    model.option = -1;
    return true;
  }
  function choose(model, index) {
    if (index < 0 || index >= model.ask.options.length) return false;
    model.option = model.option === index ? -1 : index;
    model.cards.clear(); model.targets.clear();
    return true;
  }
  function valid(model) {
    if (!model) return false;
    const a = model.ask;
    if (model.option >= 0) return model.option < a.options.length;
    if (a.kind === 'option') return false;
    const target = a.kind === 'target', set = target ? model.targets : model.cards;
    const [min, max] = bounds(model, target);
    return set.size >= min && (max <= 0 || set.size <= max) && (min > 0 || set.size > 0);
  }
  function answer(model, cancel = false) {
    if (!model || (cancel ? model.ask.allowCancel === false : !valid(model))) return null;
    return { ready: true, requestId: model.ask.requestId, option: cancel ? -1 : model.option,
      cards: cancel ? [] : [...model.cards], targets: cancel ? [] : [...model.targets] };
  }
  return { create, bounds, toggle, choose, valid, answer };
})();
