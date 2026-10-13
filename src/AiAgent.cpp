#include "AiAgent.h"

#include <algorithm>

#include "Room.h"
#include "Util.h"

namespace sgs {

// 手牌价值（越大越舍不得弃）
static int cardValue(Room& room, int cardId) {
    const Card& c = room.card(cardId);
    const std::string& n = c.name();
    if (n == CardName::Peach) return 100;
    if (n == CardName::Nullification) return 92;
    if (n == CardName::ExNihilo) return 88;
    if (n == CardName::Snatch) return 82;
    if (n == CardName::Dismantlement) return 80;
    if (n == CardName::Jink) return 72;
    if (n == CardName::Slash) return 62;
    if (n == CardName::Duel) return 50;
    if (n == CardName::Indulgence) return 46;
    if (n == CardName::Harvest) return 42;
    if (n == CardName::BarbarianInvasion) return 38;
    if (n == CardName::ArrowBarrage) return 37;
    if (n == CardName::PeachGarden) return 36;
    if (n == CardName::Collateral) return 30;
    if (n == CardName::Lightning) return 4;
    if (c.proto.type == CardType::Equip) return 20;
    return 25;
}

// 已有同槽装备时，只有明确更强才替换（避免 AI 反复换装）
static bool equipWorth(Room& room, int who, int cardId) {
    const CardProto& p = room.card(cardId).proto;
    int cur = room.equipId(who, p.slot);
    if (cur < 0) return true;
    if (p.slot == EquipSlot::Weapon) return p.range > room.card(cur).proto.range;
    return false;
}

AskResult AiAgent::ask(Room& room, const Ask& a) {
    switch (a.kind) {
        case AskKind::Card: return askCard(room, a);
        case AskKind::MultiCard: return askMultiCard(room, a);
        case AskKind::Option: return askOption(room, a);
        case AskKind::Target: return askTarget(room, a);
    }
    return AskResult{};
}

AskResult AiAgent::askCard(Room& room, const Ask& a) {
    AskResult r;
    if (a.cards.empty()) return r;
    const std::string& tag = a.tag;

    if (tag == CardName::Jink || tag == CardName::Slash) {
        r.cards = {a.cards.front()};  // 该出【闪】/【杀】就出
        return r;
    }
    if (tag == CardName::Peach) {
        if (a.subject == a.who) r.cards = {a.cards.front()};  // 只救自己
        return r;
    }
    if (tag == CardName::Nullification) {
        if (a.context == "revert") return r;   // 不把已经生效的锦囊顶回去
        if (a.source == a.who) return r;       // 自己使用的锦囊不无懈（否则坑自己）
        if (a.subject == a.who) r.cards = {a.cards.front()};  // 只保护自己
        return r;
    }
    if (tag == "Dismantle" || tag == "Snatch") {
        int best = -1;
        for (int c : a.cards) {
            Area ar = room.areaOf(c);
            if (ar == Area::Equip) { best = c; break; }
            if (ar == Area::Judge && best < 0) best = c;
        }
        r.cards = {best >= 0 ? best : a.cards.front()};
        return r;
    }
    if (tag == "Discard") {
        int best = a.cards.front();
        for (int c : a.cards) {
            if (cardValue(room, c) < cardValue(room, best)) best = c;
        }
        r.cards = {best};
        return r;
    }
    r.cards = {a.cards.front()};
    return r;
}

AskResult AiAgent::askMultiCard(Room& room, const Ask& a) {
    AskResult r;
    if (a.minCards <= 0) return r;

    std::vector<int> sorted = a.cards;
    std::sort(sorted.begin(), sorted.end(),
              [&](int x, int y) { return cardValue(room, x) < cardValue(room, y); });

    // 贯石斧要付两张牌的代价：只在对手残血时才值得
    if (a.tag == "AxeCost") {
        int enemy = room.other(a.who);
        if (room.player(enemy).hp > 2) return r;
    }
    for (int c : sorted) {
        if (static_cast<int>(r.cards.size()) >= a.minCards) break;
        r.cards.push_back(c);
    }
    return r;
}

AskResult AiAgent::askOption(Room& room, const Ask& a) {
    AskResult r;

    if (a.tag == "Harvest") {
        if (a.optionCards.empty()) return r;
        int best = 0;
        for (int i = 1; i < static_cast<int>(a.optionCards.size()); ++i) {
            if (cardValue(room, a.optionCards[i]) > cardValue(room, a.optionCards[best])) best = i;
        }
        r.option = best;
        return r;
    }
    if (a.tag == "DoubleSword") {
        r.option = a.options.empty() ? -1 : 0;
        return r;
    }
    if (a.tag == "EightDiagram") {
        // 判定基本是白嫖：成功省一张手牌【闪】，失败也还能拿手牌【闪】兜底
        r.option = 0;
        return r;
    }
    if (a.tag != "Play") {
        r.option = a.options.empty() ? -1 : 0;
        return r;
    }

    // 出牌阶段：给每个选项打分，全为 0 则结束出牌阶段
    Player& me = room.player(a.who);
    int enemy = room.other(a.who);
    int enemyHp = room.player(enemy).hp;

    int bestIdx = -1;
    int bestScore = 0;
    for (int i = 0; i < static_cast<int>(a.optionCards.size()); ++i) {
        int cid = a.optionCards[i];
        std::string n;
        bool equip = false;
        if (cid >= 0) {
            n = room.card(cid).name();
            equip = room.card(cid).proto.type == CardType::Equip;
        } else {
            n = CardName::Slash;  // 丈八蛇矛的虚拟【杀】
        }

        int score = 0;
        if (equip) {
            score = equipWorth(room, a.who, cid) ? 74 : 0;
        } else if (n == CardName::ExNihilo) {
            score = 90;
        } else if (n == CardName::Dismantlement) {
            score = 86;
        } else if (n == CardName::Snatch) {
            score = 85;
        } else if (n == CardName::Peach) {
            score = me.hp <= 2 ? 80 : 0;
        } else if (n == CardName::Harvest) {
            score = 76;
        } else if (n == CardName::Indulgence) {
            score = 73;
        } else if (n == CardName::Duel) {
            // 自己手上没【杀】就别决斗，否则等于自杀
            size_t mySlash = room.handCardsNamed(a.who, CardName::Slash).size();
            score = mySlash >= 2 ? 70 : (mySlash == 1 ? 30 : 0);
        } else if (n == CardName::BarbarianInvasion || n == CardName::ArrowBarrage) {
            score = 68;
        } else if (n == CardName::Collateral) {
            score = room.handCardsNamed(enemy, CardName::Slash).empty() ? 66 : 18;
        } else if (n == CardName::PeachGarden) {
            score = me.wounded() ? 60 : 0;
        } else if (n == CardName::Lightning) {
            score = (me.hp >= 3 && enemyHp <= me.hp) ? 30 : 0;
        } else if (n == CardName::Slash) {
            score = 52;
        } else {
            score = 10;
        }

        // 能直接收拾残局就优先
        if (score > 0 && enemyHp <= 1 &&
            (n == CardName::Slash || n == CardName::Duel ||
             n == CardName::BarbarianInvasion || n == CardName::ArrowBarrage)) {
            score += 20;
        }
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }
    r.option = (bestIdx >= 0 && bestScore > 0) ? bestIdx : -1;
    return r;
}

AskResult AiAgent::askTarget(Room& room, const Ask& a) {
    AskResult r;
    int need = std::max(1, a.minTargets);
    std::vector<int> cands = a.targetCandidates;
    std::sort(cands.begin(), cands.end(), [&](int x, int y) {
        return room.player(x).hp < room.player(y).hp;  // 优先打残血的
    });
    for (int t : cands) {
        if (static_cast<int>(r.targets.size()) >= need) break;
        r.targets.push_back(t);
    }
    return r;
}

}  // namespace sgs
