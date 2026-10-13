// 定向测试：贯石斧代价候选不得包含贯石斧自身。
#include <cstdio>
#include <memory>

#include "AiAgent.h"
#include "CardDatabase.h"
#include "CardEffects.h"
#include "Room.h"
#include "Util.h"
#include "features.h"

using namespace sgs;

static int g_axeId = -1;
static bool g_checked = false;

// P0 的代理：记录 AxeCost 候选，其余交给 AiAgent
class AxeSpy : public Agent {
public:
    AxeSpy() : inner_(std::make_unique<AiAgent>("spy")) {}
    std::string name() const override { return "spy"; }
    AskResult ask(Room& room, const Ask& a) override {
        if (a.tag == "AxeCost") {
            g_checked = true;
            bool hasAxe = false;
            std::printf("AxeCost 候选: ");
            for (int c : a.cards) {
                std::printf("%s ", room.cardLabel(c).c_str());
                if (c == g_axeId) hasAxe = true;
            if (c == ind && (g_fail = true)) std::printf("[FAIL] 候选包含判定区牌！");
            }
            std::printf("\n");
            if (hasAxe) { std::printf("[FAIL] 候选包含贯石斧自身！\n"); g_fail = true; }
            else std::printf("[ok] 候选不含贯石斧自身\n");
            return AskResult{};  // 不弃牌（放弃强命）
        }
        return inner_->ask(room, a);
    }
    static bool g_fail;
    std::unique_ptr<Agent> inner_;
};
bool AxeSpy::g_fail = false;

int main() {
    std::vector<CardProto> deck;
    std::string err;
    if (!loadDeckFromCsv("data/standard_cards.csv", deck, &err)) {
        printf("deck fail\n");
        return 1;
    }
    registerCardVocab(deck);
    Room room(deck, 7);
    room.setSilent(false);
    room.setMaxTurns(50);
    registerStandardCards(room);
    room.setupPlayer(0, "P0", true, 4, std::make_unique<AxeSpy>());
    room.setupPlayer(1, "P1", false, 4, std::make_unique<AiAgent>("a1"));

    // 找牌：贯石斧 / 杀 / 两张其他牌 / 闪
    int slash = -1, jink = -1, other1 = -1, other2 = -1;
    for (size_t i = 0; i < deck.size(); ++i) {
        const std::string& n = deck[i].name;
        if (n == CardName::Axe && g_axeId < 0) g_axeId = static_cast<int>(i);
        if (n == CardName::Slash && slash < 0) slash = static_cast<int>(i);
        if (n == CardName::Jink && jink < 0) jink = static_cast<int>(i);
        if (n == CardName::Peach && other1 < 0) other1 = static_cast<int>(i);
        if (n == CardName::ExNihilo && other2 < 0) other2 = static_cast<int>(i);
    }
    // 从牌堆/他人手里先把目标牌抽走，避免同 id 出现在两处
    for (int cid : {g_axeId, slash, other1, other2, jink}) room.removeCard(cid);
    // P0：贯石斧上装备区，手牌 = 杀 + 桃 + 无中生有
    room.equipCard(0, g_axeId);
    room.addCard(slash, 0, Area::Hand);
    room.addCard(other1, 0, Area::Hand);
    room.addCard(other2, 0, Area::Hand);
    // P1：手牌只有闪（必闪）
    room.player(1).hand.clear();
    room.addCard(jink, 1, Area::Hand);
    // 其余起始手牌清空以控制变量
    auto clearExtra = [&](int who, std::vector<int> keep) {
        auto& h = room.player(who).hand;
        std::vector<int> nh;
        for (int c : h) if (contains(keep, c)) nh.push_back(c);
        h = nh;
    };
    clearExtra(0, {slash, other1, other2});
    clearExtra(1, {jink});

    // 判定区塞一张牌（乐不思蜀）——按规则不得作为贯石斧代价
    int ind = -1;
    for (size_t i = 0; i < deck.size(); ++i)
        if (deck[i].name == CardName::Indulgence) { ind = static_cast<int>(i); break; }
    room.removeCard(ind);
    room.putJudgeCard(0, ind);

    room.runPlayPhase(0);   // P0 出杀 → P1 闪 → 贯石斧询问
    printf("%s (checked=%d)\n", AxeSpy::g_fail ? "FAIL" : (g_checked ? "PASS" : "未触发询问"),
           g_checked ? 1 : 0);
    return AxeSpy::g_fail ? 1 : 0;
}
