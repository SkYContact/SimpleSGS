// 定向复现：对手空城（无手牌/装备/判定区）时【顺手牵羊】【过河拆桥】的行为。
// 直接驱动 runPlayPhase，P0 手上只有目标牌，P1 全空。
#include <cstdio>
#include <memory>

#include "AiAgent.h"
#include "CardDatabase.h"
#include "CardEffects.h"
#include "Room.h"
#include "features.h"

using namespace sgs;

static void trial(const std::vector<CardProto>& deck, const char* name) {
    printf("=== %s ===\n", name);
    Room room(deck, 42);
    room.setSilent(false);
    room.setMaxTurns(50);
    registerStandardCards(room);
    room.setupPlayer(0, "P0", true, 4, std::make_unique<AiAgent>("a0"));
    room.setupPlayer(1, "P1", false, 4, std::make_unique<AiAgent>("a1"));
    int cid = -1;
    for (size_t i = 0; i < deck.size(); ++i) {
        if (deck[i].name == name) { cid = static_cast<int>(i); break; }
    }
    // run() 之外直接驱动出牌阶段：P0 手牌只有目标牌，P1 空城
    room.addCard(cid, 0, Area::Hand);
    // 场景开关：环境变量 JUDGE=1 时给 P1 塞一张闪电在判定区
    if (getenv("JUDGE")) {
        int lid = -1;
        for (size_t i = 0; i < deck.size(); ++i)
            if (deck[i].name == CardName::Lightning) { lid = static_cast<int>(i); break; }
        room.putJudgeCard(1, lid);
    }
    room.runPlayPhase(0);
    printf("--- 出牌阶段返回，P0 手牌 %zu 张，牌守恒=%d ---\n\n",
           room.handOf(0).size(), room.totalCardsTracked());
}

int main() {
    std::vector<CardProto> deck;
    std::string err;
    if (!loadDeckFromCsv("data/standard_cards.csv", deck, &err)) {
        printf("deck load fail: %s\n", err.c_str());
        return 1;
    }
    registerCardVocab(deck);
    trial(deck, CardName::Snatch);
    trial(deck, CardName::Dismantlement);
    return 0;
}
