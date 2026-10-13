#include "features.h"

#include <algorithm>
#include <cstring>
#include <functional>

#include "Agent.h"
#include "Player.h"
#include "Room.h"

namespace sgs {

// ---- 卡名词表：进程内注册（开局前由宿主 registerCardVocab 注入，
//      词表本身随 emit 文件头落盘，Python 侧从文件头读，不硬编码）----
namespace {
std::vector<std::string> g_card_vocab;  // 卡名 -> code
}

void registerCardVocab(const std::vector<CardProto>& deck) {
    g_card_vocab.clear();
    for (const CardProto& p : deck) {
        if (std::find(g_card_vocab.begin(), g_card_vocab.end(), p.name) == g_card_vocab.end()) {
            g_card_vocab.push_back(p.name);
        }
    }
}

const std::vector<std::string>& cardVocab() { return g_card_vocab; }

int nameCode(const std::string& name) {
    for (int i = 0; i < static_cast<int>(g_card_vocab.size()); ++i) {
        if (g_card_vocab[i] == name) return i;
    }
    return -1;
}

int cardCodeOf(const Room& room, int cardId) {
    return cardId >= 0 ? nameCode(room.card(cardId).name()) : -1;
}

SgsFeatures::SgsFeatures() {
    slot_code.assign(SEAT_N, -1);
    slot_attr.assign(static_cast<size_t>(SEAT_N) * SLOT_ATTR, 0.f);
    cand_code.assign(CAND_CAP, -1);
    cand_attr.assign(static_cast<size_t>(CAND_CAP) * CAND_ATTR, 0.f);
    cand_mask.assign(CAND_CAP, 0.f);
}

const std::vector<const char*>& tagTable() {
    static const std::vector<const char*> kTags = {
        "Play", "Target", "Jink", "Slash", "Peach", "Nullification",
        "Dismantle", "Snatch", "Discard", "Harvest", "DoubleSword",
        "EightDiagram", "AxeCost", "ViewAs", "KylinBow", "UNKNOWN",
    };
    return kTags;
}

int tagCode(const std::string& tag) {
    const auto& t = tagTable();
    for (int i = 0; i < static_cast<int>(t.size()); ++i) {
        if (tag == t[i]) return i;
    }
    return static_cast<int>(t.size()) - 1;  // UNKNOWN
}

namespace {

float encSuit(Suit s) { return static_cast<float>(static_cast<int>(s)); }

// 候选行的公共填法（CAND_ATTR 布局见 features.h）
void fillCardAttrs(float* row, const Room& room, int cardId, const Ask& a, int self) {
    const Card& c = room.card(cardId);
    row[2] = static_cast<float>(cardCodeOf(room, cardId));
    row[3] = encSuit(c.proto.suit);
    row[4] = static_cast<float>(c.proto.rank) / 13.f;
    row[5] = a.hidden ? 1.f : 0.f;
    row[6] = 0.f;
    row[7] = room.areaOf(cardId) == Area::Hand && room.ownerOf(cardId) == self ? 1.f : 0.f;
    row[8] = c.proto.type == CardType::Equip
                 ? static_cast<float>(static_cast<int>(c.proto.slot))
                 : -1.f;
    row[9] = 0.f;
}

}  // namespace

SgsFeatures buildFeatures(const Room& room, const Ask& a, int self) {
    SgsFeatures f;
    int opp = room.other(self);
    const Player& me = room.player(self);
    const Player& en = room.player(opp);

    // ---- 标量（布局见 features.h 注释）----
    float* s = f.scal;
    s[0] = static_cast<float>(room.turnCount());
    s[1] = static_cast<float>(room.currentPlayer());   // 当前行动座位（引擎不暴露阶段枚举）
    s[2] = static_cast<float>(self);
    s[3] = static_cast<float>(me.hp);
    s[4] = static_cast<float>(me.maxHp);
    s[5] = static_cast<float>(me.handCount());
    s[6] = static_cast<float>(me.slashUsed);
    s[7] = static_cast<float>(room.distance(self, opp));
    s[8] = static_cast<float>(room.attackRange(self));
    s[9] = room.inAttackRange(self, opp) ? 1.f : 0.f;
    s[10] = static_cast<float>(en.hp);
    s[11] = static_cast<float>(en.maxHp);
    s[12] = static_cast<float>(en.handCount());
    s[13] = static_cast<float>(en.slashUsed);
    s[14] = static_cast<float>(room.attackRange(opp));
    s[15] = room.inAttackRange(opp, self) ? 1.f : 0.f;
    s[16] = static_cast<float>(room.drawPileSize()) / 100.f;
    s[17] = static_cast<float>(room.discardPileSize()) / 100.f;
    s[18] = me.alive ? 1.f : 0.f;
    s[19] = en.alive ? 1.f : 0.f;
    s[20] = static_cast<float>(static_cast<int>(a.kind));
    auto rel = [&](int v) { return v == self ? 2.f : (v == opp ? 1.f : 0.f); };
    s[21] = rel(a.subject);
    s[22] = rel(a.source);
    s[23] = (a.tag == "Nullification" && a.context == "revert") ? -1.f      // 无懈链 revert
            : (a.tag == "Nullification" ? 1.f : 0.f);                       // 无懈链 cancel

    // ---- 座位槽 ----
    auto putSlot = [&](int idx, const Card* c, Area area, int owner) {
        if (!c) return;
        f.slot_code[idx] = cardCodeOf(room, c->id);
        float* r = &f.slot_attr[static_cast<size_t>(idx) * SLOT_ATTR];
        r[0] = encSuit(c->proto.suit);
        r[1] = static_cast<float>(c->proto.rank) / 13.f;
        r[2] = static_cast<float>(static_cast<int>(area));
        r[3] = static_cast<float>(owner);
    };
    for (int seat = 0; seat < 2; ++seat) {
        const Player& p = room.player(seat);
        for (int i = 0; i < HAND_CAP; ++i) {
            if (i < p.handCount())
                putSlot(slotHand(seat, i), &room.card(p.hand[i]), Area::Hand, seat);
        }
        for (int k = 0; k < EQUIP_N; ++k) {
            int eid = p.equipAt(static_cast<EquipSlot>(k));
            if (eid >= 0)
                putSlot(slotEquip(seat, static_cast<EquipSlot>(k)), &room.card(eid), Area::Equip, seat);
        }
        for (int i = 0; i < JUDGE_N; ++i) {
            if (i < static_cast<int>(p.judgeZone.size()))
                putSlot(slotJudge(seat, i), &room.card(p.judgeZone[i]), Area::Judge, seat);
        }
    }

    // ---- 候选铺行 ----
    int n = 0;
    auto put = [&](const std::function<void(float*)>& fill, int cardId, int optIdx, int targetId) {
        if (n >= CAND_CAP) return;
        float* r = &f.cand_attr[static_cast<size_t>(n) * CAND_ATTR];
        std::fill(r, r + CAND_ATTR, 0.f);
        r[0] = static_cast<float>(static_cast<int>(a.kind));
        r[1] = static_cast<float>(tagCode(a.tag));
        fill(r);
        f.cand_code[n] = static_cast<int>(r[2]);
        f.cand_mask[n] = 1.f;
        f.cand_card_id.push_back(cardId);
        f.cand_option_idx.push_back(optIdx);
        f.cand_target_id.push_back(targetId);
        ++n;
    };

    switch (a.kind) {
        case AskKind::Card:
        case AskKind::MultiCard:
            for (int cid : a.cards) {
                put([&](float* r) { fillCardAttrs(r, room, cid, a, self); }, cid, -1, -1);
            }
            break;
        case AskKind::Option:
            for (int i = 0; i < static_cast<int>(a.optionCards.size()); ++i) {
                int cid = a.optionCards[i];
                if (cid >= 0) {
                    put([&](float* r) { fillCardAttrs(r, room, cid, a, self); }, cid, i, -1);
                } else {
                    // 虚拟牌候选（丈八蛇矛视为出【杀】）：借用【杀】的词表 code
                    int sc = nameCode(CardName::Slash);
                    put([&](float* r) { r[2] = static_cast<float>(sc); }, -1, i, -1);
                }
            }
            break;
        case AskKind::Target:
            for (int t : a.targetCandidates) {
                put([&](float* r) {
                    r[2] = -1.f;  // 目标候选不占卡牌词表
                    r[9] = static_cast<float>(room.player(t).hp);
                    r[7] = t == self ? 1.f : 0.f;
                }, -1, -1, t);
            }
            break;
    }

    // ---- 合成「放弃」候选 ----
    bool mayCancel = false;
    switch (a.kind) {
        case AskKind::Card:      mayCancel = a.minCards == 0; break;
        case AskKind::MultiCard: mayCancel = a.minCards == 0; break;
        case AskKind::Option:    mayCancel = a.allowCancel; break;
        case AskKind::Target:    mayCancel = a.minTargets == 0; break;
    }
    if (mayCancel && n < CAND_CAP) {
        float* r = &f.cand_attr[static_cast<size_t>(n) * CAND_ATTR];
        std::fill(r, r + CAND_ATTR, 0.f);
        r[0] = static_cast<float>(static_cast<int>(a.kind));
        r[1] = static_cast<float>(tagCode(a.tag));
        r[6] = 1.f;  // is_decline
        f.cand_code[n] = -1;
        f.cand_mask[n] = 1.f;
        f.cand_card_id.push_back(-1);
        f.cand_option_idx.push_back(-1);
        f.cand_target_id.push_back(-1);
        f.decline_index = n;
        ++n;
    }
    return f;
}

}  // namespace sgs
