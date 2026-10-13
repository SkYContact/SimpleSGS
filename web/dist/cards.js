/* 牌面只负责显示；合法性与选项均来自 C++。图集顺序见 assets/README.md。 */
'use strict';
window.SgsCards = (() => {
  const basic = ['Slash','Jink','Peach','Dismantlement','Snatch','ExNihilo','Duel','Collateral','PeachGarden','BarbarianInvasion','ArrowBarrage','Harvest','Nullification','Indulgence','Lightning'];
  const equipment = ['ZhugeCrossbow','QinggangSword','DoubleSword','GreenDragonBlade','Spear','Axe','Halberd','KylinBow','EightDiagram','RenwangShield','RedHare','Dawan','Zixing','Dilu','Jueying','ZhuahuangFeidian'];
  const rules = {
    Slash:'出牌阶段限一次，对攻击范围内的目标使用。目标须打出闪，否则受到 1 点伤害。',
    Jink:'响应杀或万箭齐发时打出，抵消本次效果。不能在出牌阶段主动使用。',
    Peach:'出牌阶段回复自己 1 点体力；濒死时可用于救援。体力不能超过上限。',
    Dismantlement:'弃置另一名角色手牌、装备区或判定区里的一张牌。选择手牌时不能查看牌面。',
    Snatch:'获得距离 1 以内另一名角色区域里的一张牌。选择手牌时不能查看牌面。',
    ExNihilo:'摸两张牌。', Duel:'目标先打出杀，然后双方轮流打出杀。先不打出的一方受到另一方造成的 1 点伤害。',
    Collateral:'令持有武器的对手对你使用杀；若其不使用，你获得其武器。',
    PeachGarden:'所有存活角色各回复 1 点体力。', BarbarianInvasion:'其他角色须打出杀，否则受到 1 点伤害。',
    ArrowBarrage:'其他角色须打出闪，否则受到 1 点伤害。', Harvest:'亮出与存活人数相同数量的牌，各角色依次选择一张获得。',
    Nullification:'抵消锦囊对一个目标的效果。也可抵消另一张无懈可击，使原效果恢复。',
    Indulgence:'放入对方判定区。判定结果不是红桃，跳过其出牌阶段。',
    Lightning:'放入自己的判定区。判定为黑桃 2–9，受到 3 点雷电伤害；否则传给下一名角色。',
    ZhugeCrossbow:'攻击范围 1。出牌阶段使用杀不受次数限制。', QinggangSword:'攻击范围 2。使用杀时无视目标防具。',
    DoubleSword:'攻击范围 2。对异性目标使用杀时，令其选择弃一张手牌或让你摸一张牌。本项目先手男、后手女。',
    GreenDragonBlade:'攻击范围 3。杀被闪抵消后，可以再对该目标使用杀。',
    Spear:'攻击范围 3。可将两张手牌当作一张杀使用。', Axe:'攻击范围 3。杀被闪抵消后，可弃置两张牌强制命中。',
    Halberd:'攻击范围 4。最后一张手牌为杀时可增加目标；当前为双人对局。', KylinBow:'攻击范围 5。杀造成伤害后，可以弃置目标的一张坐骑。',
    EightDiagram:'需要闪时可以发动判定：红色结果视为打出闪。判定失败仍可使用手牌中的闪。',
    RenwangShield:'黑色的杀对你无效。',
    RedHare:'进攻坐骑。你计算与其他角色的距离时 −1。', Dawan:'进攻坐骑。你计算与其他角色的距离时 −1。', Zixing:'进攻坐骑。你计算与其他角色的距离时 −1。',
    Dilu:'防御坐骑。其他角色计算与你的距离时 +1。', Jueying:'防御坐骑。其他角色计算与你的距离时 +1。', ZhuahuangFeidian:'防御坐骑。其他角色计算与你的距离时 +1。'
  };
  function escape(value) { return String(value ?? '').replace(/[&<>"']/g, ch => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[ch])); }
  function art(card) {
    let index = basic.indexOf(card.name), columns = 5, rows = 3, file = 'cards';
    if (index < 0) { index = equipment.indexOf(card.name); columns = rows = 4; file = 'equipment'; }
    if (index < 0) return '';
    return `background-image:url(assets/${file}.webp);background-size:${columns * 100}% ${rows * 100}%;background-position:${index % columns / (columns - 1) * 100}% ${Math.floor(index / columns) / (rows - 1) * 100}%`;
  }
  function face(card, extra = '') {
    if (card.hidden) return '<span class="playing-card card-back"><span class="unknown-label">未知手牌</span></span>';
    const red = card.suit === '♥' || card.suit === '♦';
    return `<span class="playing-card ${red ? 'red-suit' : ''} ${extra}"><span class="card-art" style="${art(card)}"></span><span class="card-corner"><span>${escape(card.rank)}</span><span>${escape(card.suit)}</span></span><span class="card-name ${String(card.display || '').length > 3 ? 'long-name' : ''}">${escape(card.display || card.label)}</span></span>`;
  }
  return { escape, art, face, rules };
})();
