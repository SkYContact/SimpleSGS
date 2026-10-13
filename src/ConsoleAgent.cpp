#include "ConsoleAgent.h"

#include <cctype>
#include <iostream>

#include "Room.h"
#include "Util.h"

namespace sgs {

namespace {

std::string readLine() {
    std::string line;
    if (!std::getline(std::cin, line)) return "q";  // EOF 视为放弃
    return line;
}

// 从一行文本里抽出所有整数，支持 "1 2" / "1,2" / "1、2"
bool parseNumbers(const std::string& line, std::vector<int>& out) {
    std::string cur;
    for (char c : line + " ") {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            cur.push_back(c);
        } else if (!cur.empty()) {
            out.push_back(toInt(cur, -1));
            cur.clear();
        }
    }
    return !out.empty();
}

bool cancelInput(const std::string& line) {
    for (char c : line) {
        if (c == 'q' || c == 'Q' || c == 'p' || c == 'P') return true;
    }
    return false;
}

}  // namespace

void ConsoleAgent::printStatus(Room& room, int who) const {
    const Player& me = room.player(who);
    const Player& foe = room.player(room.other(who));

    std::string hand;
    for (size_t i = 0; i < me.hand.size(); ++i) {
        if (i) hand += " ";
        hand += room.cardLabel(me.hand[i]);
    }
    std::string eq;
    static const char* slotName[4] = {"武器", "防具", "+1马", "-1马"};
    for (int i = 0; i < 4; ++i) {
        if (me.equip[i] >= 0) {
            if (!eq.empty()) eq += " ";
            eq += str(slotName[i], "=", room.cardLabel(me.equip[i]));
        }
    }
    std::string jz;
    for (size_t i = 0; i < me.judgeZone.size(); ++i) {
        if (i) jz += " ";
        jz += room.cardLabel(me.judgeZone[i]);
    }

    std::cout << "  --------------------------------------------------\n";
    std::cout << str("  [我] 体力 ", me.hp, "/", me.maxHp, " | 手牌(", me.handCount(), "): ",
                     hand.empty() ? "(空)" : hand, "\n");
    std::cout << str("       装备: ", eq.empty() ? "(无)" : eq, "  判定区: ", jz.empty() ? "(无)" : jz,
                     "\n");
    std::cout << str("  [敌] 体力 ", foe.hp, "/", foe.maxHp, " | 手牌 ", foe.handCount(), " 张",
                     " | 攻击范围 ", room.attackRange(foe.id), " | 与你的距离 ", room.distance(who, foe.id),
                     "\n");
    std::cout << "  --------------------------------------------------\n";
}

AskResult ConsoleAgent::ask(Room& room, const Ask& a) {
    AskResult r;
    if (a.who < 0) return r;

    std::cout << "\n>>> " << room.playerName(a.who) << "：" << a.prompt << "\n";

    switch (a.kind) {
        case AskKind::Card:
        case AskKind::Target: {
            printStatus(room, a.who);
            if (a.kind == AskKind::Card) {
                for (size_t i = 0; i < a.cards.size(); ++i) {
                    int c = a.cards[i];
                    bool hidden = a.hidden && room.areaOf(c) == Area::Hand;
                    if (hidden) {
                        std::cout << str("  ", i + 1, ". 手牌(未知)\n");
                    } else {
                        std::cout << str("  ", i + 1, ". ", room.cardLabel(c), "  [",
                                         areaName(room.areaOf(c)), "]\n");
                    }
                }
            } else {
                for (size_t i = 0; i < a.targetCandidates.size(); ++i) {
                    int t = a.targetCandidates[i];
                    std::cout << str("  ", i + 1, ". ", room.playerName(t), "（体力 ",
                                     room.player(t).hp, "/", room.player(t).maxHp, "）\n");
                }
            }
            std::cout << "  0. 放弃（q 也可）\n> ";
            std::string line = readLine();
            std::vector<int> nums;
            if (cancelInput(line) || !parseNumbers(line, nums)) return r;

            int total = (a.kind == AskKind::Card) ? static_cast<int>(a.cards.size())
                                                  : static_cast<int>(a.targetCandidates.size());
            std::vector<int> picked;
            for (int num : nums) {
                if (num <= 0 || num > total) continue;
                int id = (a.kind == AskKind::Card) ? a.cards[num - 1] : a.targetCandidates[num - 1];
                if (!contains(picked, id)) picked.push_back(id);
                if (a.kind == AskKind::Card && a.maxCards == 1) break;
            }
            if (picked.empty()) return r;
            if (a.kind == AskKind::Card) {
                if (static_cast<int>(picked.size()) > a.maxCards && a.maxCards > 0) {
                    picked.resize(a.maxCards);
                }
                r.cards = picked;
            } else {
                if (a.minTargets > 0 && static_cast<int>(picked.size()) < a.minTargets) {
                    std::cout << "  目标数量不足，视为放弃。\n";
                    return r;
                }
                r.targets = picked;
            }
            return r;
        }

        case AskKind::MultiCard: {
            printStatus(room, a.who);
            for (size_t i = 0; i < a.cards.size(); ++i) {
                std::cout << str("  ", i + 1, ". ", room.cardLabel(a.cards[i]), "\n");
            }
            std::cout << str("  请选择 ", a.minCards, " 张（空格/逗号分隔，q 放弃）\n> ");
            std::string line = readLine();
            std::vector<int> nums;
            if (cancelInput(line) || !parseNumbers(line, nums)) return r;
            for (int num : nums) {
                if (num <= 0 || num > static_cast<int>(a.cards.size())) continue;
                int id = a.cards[num - 1];
                if (!contains(r.cards, id)) r.cards.push_back(id);
            }
            if (static_cast<int>(r.cards.size()) != a.minCards) {
                std::cout << "  选择数量不符，视为放弃。\n";
                return AskResult{};
            }
            return r;
        }

        case AskKind::Option: {
            printStatus(room, a.who);
            for (size_t i = 0; i < a.options.size(); ++i) {
                std::cout << str("  ", i + 1, ". ", a.options[i], "\n");
            }
            std::cout << "  0. 放弃/结束\n> ";
            std::string line = readLine();
            std::vector<int> nums;
            if (cancelInput(line) || !parseNumbers(line, nums)) return r;
            int num = nums.front();
            if (num <= 0 || num > static_cast<int>(a.options.size())) return r;
            r.option = num - 1;
            return r;
        }
    }
    return r;
}

}  // namespace sgs
