#include "Types.h"

#include "Util.h"

namespace sgs {

const char* suitName(Suit s) {
    switch (s) {
        case Suit::Spade: return "黑桃";
        case Suit::Heart: return "红桃";
        case Suit::Club: return "梅花";
        case Suit::Diamond: return "方块";
        default: return "无色";
    }
}

const char* suitSymbol(Suit s) {
    switch (s) {
        case Suit::Spade: return "♠";
        case Suit::Heart: return "♥";
        case Suit::Club: return "♣";
        case Suit::Diamond: return "♦";
        default: return "";
    }
}

const char* typeName(CardType t) {
    switch (t) {
        case CardType::Basic: return "基本牌";
        case CardType::Trick: return "锦囊牌";
        case CardType::DelayedTrick: return "延时锦囊";
        case CardType::Equip: return "装备牌";
        default: return "?";
    }
}

const char* phaseName(Phase p) {
    switch (p) {
        case Phase::RoundStart: return "回合开始阶段";
        case Phase::Judge: return "判定阶段";
        case Phase::Draw: return "摸牌阶段";
        case Phase::Play: return "出牌阶段";
        case Phase::Discard: return "弃牌阶段";
        case Phase::RoundEnd: return "回合结束阶段";
        default: return "空闲";
    }
}

const char* areaName(Area a) {
    switch (a) {
        case Area::Hand: return "手牌";
        case Area::Equip: return "装备区";
        case Area::Judge: return "判定区";
        case Area::DrawPile: return "牌堆";
        case Area::DiscardPile: return "弃牌堆";
        case Area::Revealed: return "亮牌区";
        default: return "离场";
    }
}

const char* natureName(DamageNature n) {
    switch (n) {
        case DamageNature::Fire: return "火属性";
        case DamageNature::Thunder: return "雷属性";
        default: return "";
    }
}

bool isRed(Suit s) { return s == Suit::Heart || s == Suit::Diamond; }
bool isBlack(Suit s) { return s == Suit::Spade || s == Suit::Club; }

const char* eightDiagramModeName(EightDiagramMode m) {
    switch (m) {
        case EightDiagramMode::Ask: return "询问";
        case EightDiagramMode::Off: return "关闭";
        default: return "自动";
    }
}

EightDiagramMode eightDiagramModeFromInt(int v) {
    if (v == 1) return EightDiagramMode::Ask;
    if (v == 2) return EightDiagramMode::Off;
    return EightDiagramMode::Auto;
}

bool parseEightDiagramMode(const std::string& s, EightDiagramMode& out) {
    if (s == "auto" || s == "自动" || s == "0") { out = EightDiagramMode::Auto; return true; }
    if (s == "ask" || s == "询问" || s == "1") { out = EightDiagramMode::Ask; return true; }
    if (s == "off" || s == "关闭" || s == "none" || s == "2") { out = EightDiagramMode::Off; return true; }
    return false;
}

std::string rankLabel(int rank) {
    switch (rank) {
        case 1: return "A";
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        default: return str(rank);
    }
}

// 装备牌的牌名后面补一个用途标记：
//   坐骑：+1马（防守马，别人算你更远） / -1马（进攻马，你算别人更近）
//   武器：射程N（因为武器牌本身看不出攻击范围）
// 不这样做的话，手牌/日志里只有【紫骍】(♦K)，玩家根本分不清是哪种马。
std::string equipTag(const CardProto& proto) {
    switch (proto.slot) {
        case EquipSlot::DefensiveHorse: return " +1马";
        case EquipSlot::OffensiveHorse: return " -1马";
        case EquipSlot::Weapon: return proto.range > 0 ? str(" 射程", proto.range) : "";
        default: return "";
    }
}

std::string cardLabel(const CardProto& proto, bool virtualCard) {
    std::string tag;
    if (!virtualCard && proto.type == CardType::Equip) tag = equipTag(proto);
    if (virtualCard || proto.suit == Suit::None) {
        return str("【", proto.display, tag, "】(无色)");
    }
    return str("【", proto.display, tag, "】(", suitSymbol(proto.suit), rankLabel(proto.rank), ")");
}

}  // namespace sgs
