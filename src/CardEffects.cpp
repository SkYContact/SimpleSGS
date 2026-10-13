#include "CardEffects.h"

#include <algorithm>

#include "Util.h"

namespace sgs {

// ============================================================ 小工具

static bool nameIs(const CardProto& p, const char* n) { return p.name == n; }

// 按座次顺序列出存活角色，从 who 开始（afterUser=true 则从 who 的下家开始）。
// 五谷丰登这类"所有人依次选"的牌，顺序会影响公平性：正式规则是【从使用者开始】。
static std::vector<int> aliveFrom(Room& room, int who, bool afterUser) {
    std::vector<int> alive = room.alivePlayers();  // 已按座位升序
    if (alive.empty()) return {};
    int idx = 0;
    for (size_t i = 0; i < alive.size(); ++i) {
        if (alive[i] == who) {
            idx = static_cast<int>(i);
            break;
        }
    }
    if (afterUser) idx = (idx + 1) % static_cast<int>(alive.size());
    std::vector<int> out;
    for (size_t i = 0; i < alive.size(); ++i) {
        out.push_back(alive[(idx + i) % alive.size()]);
    }
    return out;
}

static std::vector<int> otherAlive(Room& room, int who) {
    std::vector<int> out;
    for (int t : aliveFrom(room, who, true)) {  // 从下家开始
        if (t != who) out.push_back(t);
    }
    return out;
}

static std::vector<int> slashTargets(Room& room, int who) {
    std::vector<int> out;
    for (int t : room.alivePlayers()) {
        if (t != who && room.inAttackRange(who, t)) out.push_back(t);
    }
    return out;
}

int slashLimit(const Room& room, int who) {
    if (room.hasEquipNamed(who, EquipSlot::Weapon, CardName::ZhugeCrossbow)) return kUnlimited;
    return kBaseSlashLimit;
}

// ============================================================ 可用性判定

bool cardUsableNow(Room& room, int who, const CardProto& proto, int cardId,
                   std::vector<int>& outCandidates, int& outMin, int& outMax, bool& needAsk) {
    outCandidates.clear();
    outMin = outMax = 0;
    needAsk = false;

    Player& p = room.player(who);
    const std::string& n = proto.name;

    if (n == CardName::Slash) {
        if (p.slashUsed >= slashLimit(room, who)) return false;
        outCandidates = slashTargets(room, who);
        if (outCandidates.empty()) return false;
        outMin = 1;
        outMax = 1;
        // 方天画戟：此【杀】是最后一张手牌时，可额外指定至多两个目标
        if (room.hasEquipNamed(who, EquipSlot::Weapon, CardName::Halberd) && p.handCount() == 1) {
            outMax = std::min(3, static_cast<int>(outCandidates.size()));
        }
        needAsk = true;
        return true;
    }
    if (n == CardName::Jink || n == CardName::Nullification) return false;  // 不能主动使用
    if (n == CardName::Peach) {
        if (!p.wounded()) return false;
        outCandidates = {who};
        outMin = outMax = 1;
        return true;
    }
    if (n == CardName::ExNihilo) {
        outCandidates = {who};
        outMin = outMax = 1;
        return true;
    }
    if (n == CardName::Dismantlement) {
        int t = room.other(who);
        if (!room.player(t).alive || room.allCardsOf(t).empty()) return false;
        outCandidates = {t};
        outMin = outMax = 1;
        return true;
    }
    if (n == CardName::Snatch) {
        int t = room.other(who);
        if (!room.player(t).alive) return false;
        if (room.distance(who, t) > 1) return false;  // 距离 1 以内（防守马会拉开）
        if (room.allCardsOf(t).empty()) return false;
        outCandidates = {t};
        outMin = outMax = 1;
        return true;
    }
    if (n == CardName::Duel) {
        int t = room.other(who);
        if (!room.player(t).alive) return false;
        outCandidates = {t};
        outMin = outMax = 1;
        return true;
    }
    if (n == CardName::Collateral) {
        int t = room.other(who);
        if (!room.player(t).alive) return false;
        if (room.equipId(t, EquipSlot::Weapon) < 0) return false;
        if (!room.inAttackRange(t, who)) return false;  // 目标须能攻击到你
        outCandidates = {t};
        outMin = outMax = 1;
        return true;
    }
    if (n == CardName::Indulgence) {
        int t = room.other(who);
        if (!room.player(t).alive) return false;
        for (int c : room.player(t).judgeZone) {
            if (room.card(c).name() == CardName::Indulgence) return false;
        }
        outCandidates = {t};
        outMin = outMax = 1;
        return true;
    }
    if (n == CardName::Lightning) {
        for (int c : p.judgeZone) {
            if (room.card(c).name() == CardName::Lightning) return false;
        }
        outCandidates = {who};
        outMin = outMax = 1;
        return true;
    }
    // 多目标锦囊的【目标顺序 = 结算顺序】，必须按"从使用者开始沿座次"排，
    // 不能直接用 alivePlayers()（那是座位顺序 0,1,…，使用者可能排在后面）：
    //   五谷丰登  -> 使用者先挑牌
    //   桃园结义/南蛮入侵/万箭齐发 -> 从使用者的下家开始依次结算
    if (n == CardName::Harvest || n == CardName::PeachGarden) {
        outCandidates = aliveFrom(room, who, false);  // 使用者排第一
        outMin = outMax = static_cast<int>(outCandidates.size());
        return !outCandidates.empty();
    }
    if (n == CardName::BarbarianInvasion || n == CardName::ArrowBarrage) {
        outCandidates = otherAlive(room, who);        // 从下家开始，不含自己
        outMin = outMax = static_cast<int>(outCandidates.size());
        return !outCandidates.empty();
    }
    if (proto.type == CardType::Equip) {
        outCandidates = {who};
        outMin = outMax = 1;
        return true;
    }
    (void)cardId;
    return false;
}

// ============================================================ 出牌阶段选项

std::vector<PlayOption> buildPlayOptions(Room& room, int who) {
    std::vector<PlayOption> out;
    Player& p = room.player(who);

    for (int cid : p.hand) {
        const CardProto& proto = room.card(cid).proto;
        std::vector<int> cands;
        int mn = 0, mx = 0;
        bool need = false;
        if (!cardUsableNow(room, who, proto, cid, cands, mn, mx, need)) continue;

        PlayOption o;
        o.proto = proto;
        o.cardId = cid;
        o.cards = {cid};
        o.needAskTargets = need;
        o.targetCandidates = cands;
        o.minTargets = mn;
        o.maxTargets = mx;
        if (!need) o.presetTargets = cands;

        std::string hint;
        if (need && !cands.empty()) {
            hint = "（目标：";
            for (size_t i = 0; i < cands.size(); ++i) {
                if (i) hint += "、";
                hint += room.playerName(cands[i]);
            }
            hint += "）";
        }
        o.text = str("使用 ", room.cardLabel(cid), hint);
        out.push_back(o);
    }

    // 丈八蛇矛：两张手牌当【杀】
    if (room.hasEquipNamed(who, EquipSlot::Weapon, CardName::Spear) && p.handCount() >= 2 &&
        p.slashUsed < slashLimit(room, who)) {
        std::vector<int> cands = slashTargets(room, who);
        if (!cands.empty()) {
            PlayOption o;
            o.virtualCard = true;
            o.cardId = -1;
            o.proto.name = CardName::Slash;
            o.proto.display = "杀";
            o.proto.type = CardType::Basic;
            o.proto.suit = Suit::None;
            o.needAskTargets = true;
            o.targetCandidates = cands;
            o.minTargets = 1;
            o.maxTargets = 1;
            o.text = "【丈八蛇矛】将两张手牌当【杀】使用";
            out.push_back(o);
        }
    }
    return out;
}

// ============================================================ 单张牌效果

static void effectSlash(Room& room, CardUseStruct& use, int target) {
    int user = use.user;
    bool ignoreArmor = room.hasEquipNamed(user, EquipSlot::Weapon, CardName::QinggangSword);

    if (!ignoreArmor && room.hasEquipNamed(target, EquipSlot::Armor, CardName::RenwangShield) &&
        isBlack(use.proto.suit)) {
        room.chat("  【仁王盾】令黑色【杀】无效");
        return;
    }

    int jink = room.askForResponse(target, CardName::Jink,
                                   str("【杀】指定你为目标，是否打出【闪】？（否则受到 1 点伤害）"),
                                   user);
    if (responseOk(jink)) {
        if (jink == kVirtualCard) {
            room.chat(str("  ", room.playerName(target), " 视为打出了【闪】"));
        }
        TriggerParams tp;
        tp.event = Event::SlashDodged;
        tp.who = target;
        tp.source = user;
        tp.target = target;
        tp.use = &use;
        room.trigger(Event::SlashDodged, tp);
        if (!tp.forcedHit) return;
        room.chat("  【杀】强制命中");
    }

    DamageStruct d;
    d.from = user;
    d.to = target;
    d.cardId = use.cardId;
    d.amount = 1;
    d.fromCard = true;
    room.damage(d);
}

// 从目标区域选一张牌（手牌区内容不可见）
static int pickCardFrom(Room& room, int user, int target, const char* tag, const std::string& prompt) {
    std::vector<int> cands = room.allCardsOf(target);
    if (cands.empty()) return -1;
    Ask a;
    a.kind = AskKind::Card;
    a.tag = tag;
    a.who = user;
    a.subject = target;
    a.cards = cands;
    a.hidden = true;  // 手牌只看得到张数
    a.allowCancel = false;
    a.prompt = prompt;
    AskResult r = room.askAgent(a);
    for (int c : r.cards) {
        if (contains(cands, c)) return c;
    }
    // AI/输入异常时兜底：优先装备区/判定区，否则第一张手牌
    for (int c : cands) {
        if (room.areaOf(c) != Area::Hand) return c;
    }
    return cands[0];
}

static void effectDismantlement(Room& room, CardUseStruct& use, int target) {
    int c = pickCardFrom(room, use.user, target, "Dismantle",
                         str("【过河拆桥】：选择要弃置的 ", room.playerName(target), " 的一张牌"));
    if (c < 0) {
        room.chat("  目标没有可弃置的牌");
        return;
    }
    room.discardCards({c}, "过河拆桥");
}

static void effectSnatch(Room& room, CardUseStruct& use, int target) {
    int c = pickCardFrom(room, use.user, target, "Snatch",
                         str("【顺手牵羊】：选择要获得的 ", room.playerName(target), " 的一张牌"));
    if (c < 0) {
        room.chat("  目标没有可获得的牌");
        return;
    }
    room.moveCards({c}, use.user, Area::Hand, "顺手牵羊");
}

static void effectDuel(Room& room, CardUseStruct& use, int target) {
    int a = target;      // 由目标先出【杀】
    int b = use.user;
    int guard = 0;
    while (guard++ < 64) {
        int r = room.askForResponse(a, CardName::Slash,
                                    str("【决斗】：请打出一张【杀】（否则受到来自 ",
                                        room.playerName(b), " 的 1 点伤害）"),
                                    b);
        if (!responseOk(r)) {
            DamageStruct d;
            d.from = b;
            d.to = a;
            d.cardId = use.cardId;
            d.amount = 1;
            d.fromCard = true;
            room.damage(d);
            return;
        }
        std::swap(a, b);
    }
}

static void effectCollateral(Room& room, CardUseStruct& use, int target) {
    int user = use.user;
    int weapon = room.equipId(target, EquipSlot::Weapon);
    if (weapon < 0) return;

    bool used = false;
    std::vector<int> slashes = room.handCardsNamed(target, CardName::Slash);
    if (!slashes.empty()) {
        Ask a;
        a.kind = AskKind::Card;
        a.tag = CardName::Slash;
        a.who = target;
        a.subject = user;
        a.cards = slashes;
        a.prompt = str("【借刀杀人】：是否对 ", room.playerName(user),
                       " 使用一张【杀】？（否则你的武器交给对方）");
        AskResult r = room.askAgent(a);
        if (r.cards.size() == 1 && contains(slashes, r.cards[0])) {
            CardUseStruct u2;
            u2.user = target;
            u2.cardId = r.cards[0];
            u2.cards = {r.cards[0]};
            u2.proto = room.card(r.cards[0]).proto;
            u2.targets = {user};
            room.useCard(u2);
            used = true;
        }
    }
    if (!used && room.player(target).alive) {
        room.chat(str("  ", room.playerName(target), " 未使用【杀】，武器交给对方"));
        room.moveCards({weapon}, user, Area::Hand, "借刀杀人");
    }
}

static void effectBarbarian(Room& room, CardUseStruct& use, int target) {
    int r = room.askForResponse(target, CardName::Slash,
                                str("【南蛮入侵】：请打出一张【杀】（否则受到 1 点伤害）"), use.user);
    if (responseOk(r)) return;
    DamageStruct d;
    d.from = use.user;
    d.to = target;
    d.cardId = use.cardId;
    d.amount = 1;
    d.fromCard = true;
    room.damage(d);
}

static void effectArrowBarrage(Room& room, CardUseStruct& use, int target) {
    int r = room.askForResponse(target, CardName::Jink,
                                str("【万箭齐发】：请打出一张【闪】（否则受到 1 点伤害）"), use.user);
    if (responseOk(r)) return;
    DamageStruct d;
    d.from = use.user;
    d.to = target;
    d.cardId = use.cardId;
    d.amount = 1;
    d.fromCard = true;
    room.damage(d);
}

static void effectHarvest(Room& room, CardUseStruct& use, int target) {
    if (use.sharedCards.empty()) {
        use.sharedCards = room.revealCards(static_cast<int>(use.targets.size()));
        std::string label;
        for (size_t i = 0; i < use.sharedCards.size(); ++i) {
            if (i) label += "、";
            label += room.cardLabel(use.sharedCards[i]);
        }
        room.chat(str("  【五谷丰登】亮出：", label));
    }
    if (use.sharedCards.empty()) return;

    Ask a;
    a.kind = AskKind::Option;
    a.tag = "Harvest";
    a.allowCancel = false;
    a.who = target;
    a.prompt = "【五谷丰登】：选择一张牌获得";
    for (int c : use.sharedCards) {
        a.options.push_back(room.cardLabel(c));
        a.optionCards.push_back(c);
    }
    AskResult r = room.askAgent(a);
    int pick = -1;
    if (r.option >= 0 && r.option < static_cast<int>(a.optionCards.size())) {
        pick = a.optionCards[r.option];
    }
    if (pick < 0 || !contains(use.sharedCards, pick)) pick = use.sharedCards.front();
    room.moveCards({pick}, target, Area::Hand, "五谷丰登");
    eraseValue(use.sharedCards, pick);
}

void applyCardEffect(Room& room, CardUseStruct& use, int target) {
    const std::string& n = use.proto.name;

    if (n == CardName::Slash) return effectSlash(room, use, target);
    if (n == CardName::Peach) { room.recover(target, 1); return; }
    if (n == CardName::ExNihilo) { room.drawCards(use.user, 2); return; }
    if (n == CardName::Dismantlement) return effectDismantlement(room, use, target);
    if (n == CardName::Snatch) return effectSnatch(room, use, target);
    if (n == CardName::Duel) return effectDuel(room, use, target);
    if (n == CardName::Collateral) return effectCollateral(room, use, target);
    if (n == CardName::PeachGarden) { room.recover(target, 1); return; }
    if (n == CardName::BarbarianInvasion) return effectBarbarian(room, use, target);
    if (n == CardName::ArrowBarrage) return effectArrowBarrage(room, use, target);
    if (n == CardName::Harvest) return effectHarvest(room, use, target);

    room.chat(str("  （", n, " 尚未实现效果）"));
}

// ============================================================ 装备技能（事件注册）

void registerStandardCards(Room& room) {
    // ---- 八卦阵：需要【闪】时判定，红色则视为打出【闪】
    //      发动方式由 Room::setEightDiagramMode 决定：Auto（默认，没手牌闪才判）/ Ask / Off
    room.registerTrigger("EightDiagram", Event::AskForCard, -1, 0, [](Room& r, TriggerParams& p) {
        if (p.askName != CardName::Jink) return true;
        if (!r.player(p.who).alive) return true;
        if (!r.hasEquipNamed(p.who, EquipSlot::Armor, CardName::EightDiagram)) return true;
        if (p.source >= 0 &&
            r.hasEquipNamed(p.source, EquipSlot::Weapon, CardName::QinggangSword)) {
            r.chat("  【青釭剑】无视【八卦阵】");
            return true;
        }

        const EightDiagramMode mode = r.eightDiagramMode();
        const bool hasJink = !p.askCandidates.empty();
        if (mode == EightDiagramMode::Off) return true;
        if (mode == EightDiagramMode::Auto && hasJink) return true;  // 有实体【闪】就直接用手牌

        if (mode == EightDiagramMode::Ask) {
            Ask a;
            a.kind = AskKind::Option;
            a.tag = "EightDiagram";
            a.who = p.who;
            a.subject = p.source;
            a.options = {"发动【八卦阵】判定", "不发动"};
            a.prompt = str("【八卦阵】：是否发动判定？（红色＝视为打出【闪】",
                           hasJink ? "，你保留手牌【闪】；判定失败再问你是否打出手牌【闪】"
                                   : "，判定失败就只能承受",
                           "）");
            AskResult res = r.askAgent(a);
            if (res.option != 0) return true;
        }

        int jc = r.judge(p.who, "八卦阵");
        if (jc >= 0 && isRed(r.card(jc).proto.suit)) {
            r.chat(str("  ", r.playerName(p.who), " 的【八卦阵】判定为红色，视为打出了【闪】"));
            p.provided = true;
            p.providedCard = -1;
        } else {
            r.chat(str("  ", r.playerName(p.who), " 的【八卦阵】判定失败"));
        }
        return true;
    });

    // ---- 贯石斧：【杀】被【闪】抵消后，弃两张牌强制命中
    room.registerTrigger("Axe", Event::SlashDodged, -1, 10, [](Room& r, TriggerParams& p) {
        int owner = p.source;
        if (owner < 0 || !r.player(owner).alive) return true;
        if (!r.hasEquipNamed(owner, EquipSlot::Weapon, CardName::Axe)) return true;
        std::vector<int> cands = r.allCardsOf(owner);
        // 贯石斧自身不能作为代价弃掉（它是本技能的来源牌，且还在装备区）
        int axeId = r.equipId(owner, EquipSlot::Weapon);
        cands.erase(std::remove(cands.begin(), cands.end(), axeId), cands.end());
        if (cands.size() < 2) return true;
        Ask a;
        a.kind = AskKind::MultiCard;
        a.tag = "AxeCost";
        a.who = owner;
        a.cards = cands;
        a.minCards = 2;
        a.maxCards = 2;
        a.prompt = str("【贯石斧】：是否弃置两张牌以强制命中 ", r.playerName(p.target), "？");
        AskResult res = r.askAgent(a);
        std::vector<int> pick;
        for (int c : res.cards) {
            if (contains(cands, c) && !contains(pick, c)) pick.push_back(c);
        }
        if (pick.size() != 2) return true;
        r.discardCards(pick, "贯石斧");
        p.forcedHit = true;
        return true;
    });

    // ---- 青龙偃月刀：【杀】被【闪】抵消后，可以再使用一张【杀】
    room.registerTrigger("GreenDragonBlade", Event::SlashDodged, -1, 20,
                         [](Room& r, TriggerParams& p) {
                             int owner = p.source;
                             if (owner < 0 || !r.player(owner).alive) return true;
                             if (!r.hasEquipNamed(owner, EquipSlot::Weapon,
                                                  CardName::GreenDragonBlade)) {
                                 return true;
                             }
                             if (p.target < 0 || !r.player(p.target).alive) return true;
                             std::vector<int> slashes = r.handCardsNamed(owner, CardName::Slash);
                             if (slashes.empty()) return true;
                             Ask a;
                             a.kind = AskKind::Card;
                             a.tag = CardName::Slash;
                             a.who = owner;
                             a.subject = p.target;
                             a.cards = slashes;
                             a.prompt = str("【青龙偃月刀】：是否对 ", r.playerName(p.target),
                                            " 再使用一张【杀】？");
                             AskResult res = r.askAgent(a);
                             if (res.cards.size() != 1 || !contains(slashes, res.cards[0])) {
                                 return true;
                             }
                             CardUseStruct u2;
                             u2.user = owner;
                             u2.cardId = res.cards[0];
                             u2.cards = {res.cards[0]};
                             u2.proto = r.card(res.cards[0]).proto;
                             u2.targets = {p.target};
                             r.useCard(u2);
                             return true;
                         });

    // ---- 麒麟弓：使用【杀】造成伤害后，可弃置目标的一匹坐骑
    room.registerTrigger("KylinBow", Event::Damaged, -1, 0, [](Room& r, TriggerParams& p) {
        if (!p.damage || p.damage->from < 0) return true;
        int src = p.damage->from;
        int to = p.damage->to;
        if (!r.player(src).alive || !r.player(to).alive) return true;
        if (!r.hasEquipNamed(src, EquipSlot::Weapon, CardName::KylinBow)) return true;
        if (!p.damage->fromCard) return true;
        if (p.damage->cardId < 0) return true;
        if (r.card(p.damage->cardId).name() != CardName::Slash) return true;
        std::vector<int> horses;
        int dh = r.equipId(to, EquipSlot::DefensiveHorse);
        if (dh >= 0) horses.push_back(dh);
        int oh = r.equipId(to, EquipSlot::OffensiveHorse);
        if (oh >= 0) horses.push_back(oh);
        if (horses.empty()) return true;
        Ask a;
        a.kind = AskKind::Card;
        a.tag = "KylinBow";
        a.who = src;
        a.subject = to;
        a.cards = horses;
        a.prompt = str("【麒麟弓】：是否弃置 ", r.playerName(to), " 的一匹坐骑？");
        AskResult res = r.askAgent(a);
        if (res.cards.size() == 1 && contains(horses, res.cards[0])) {
            r.discardCards({res.cards[0]}, "麒麟弓");
        }
        return true;
    });

    // ---- 雌雄双股剑：使用【杀】指定异性目标时，令其选择弃一张手牌或你摸一张牌
    room.registerTrigger("DoubleSword", Event::CardEffect, -1, 0, [](Room& r, TriggerParams& p) {
        if (!p.use || !nameIs(p.use->proto, CardName::Slash)) return true;
        int src = p.use->user;
        int tgt = p.target;
        if (!r.player(src).alive || !r.player(tgt).alive) return true;
        if (!r.hasEquipNamed(src, EquipSlot::Weapon, CardName::DoubleSword)) return true;
        if (r.player(src).male == r.player(tgt).male) return true;

        std::vector<int> hand = r.handOf(tgt);
        if (hand.empty()) {
            r.chat(str("  【雌雄双股剑】：", r.playerName(tgt), " 无手牌，", r.playerName(src),
                       " 摸一张牌"));
            r.drawCards(src, 1);
            return true;
        }
        Ask a;
        a.kind = AskKind::Option;
        a.tag = "DoubleSword";
        a.who = tgt;
        a.subject = src;
        a.options = {"弃置一张手牌", str("令 ", r.playerName(src), " 摸一张牌")};
        a.prompt = "【雌雄双股剑】：请选择";
        AskResult res = r.askAgent(a);
        if (res.option == 1) {
            r.chat(str("  【雌雄双股剑】：", r.playerName(src), " 摸一张牌"));
            r.drawCards(src, 1);
            return true;
        }
        Ask d;
        d.kind = AskKind::Card;
        d.tag = "Discard";
        d.who = tgt;
        d.cards = hand;
        d.prompt = "【雌雄双股剑】：选择一张手牌弃置";
        AskResult dr = r.askAgent(d);
        int c = -1;
        if (dr.cards.size() == 1 && contains(hand, dr.cards[0])) c = dr.cards[0];
        if (c < 0) c = hand.front();
        r.discardCards({c}, "雌雄双股剑");
        return true;
    });
}

}  // namespace sgs
