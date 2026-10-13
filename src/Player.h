#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Agent.h"
#include "Types.h"

namespace sgs {

// 服务器权威视角的角色（QSanguosha 的 ServerPlayer 思路）
struct Player {
    int id = -1;
    std::string name;
    bool male = true;          // 无武将，性别仅用于【雌雄双股剑】
    int hp = 4;
    int maxHp = 4;
    bool alive = true;

    std::vector<int> hand;       // 手牌
    std::vector<int> judgeZone;  // 判定区（乐不思蜀 / 闪电）
    std::vector<int> equip;      // 装备区，下标 = EquipSlot

    std::unique_ptr<Agent> agent;

    // 回合内临时状态
    int slashUsed = 0;           // 本回合已使用【杀】的次数
    bool skipPlayPhase = false;  // 被【乐不思蜀】跳过出牌阶段

    Player() { equip.assign(4, -1); }
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    int equipAt(EquipSlot s) const { return equip[static_cast<int>(s)]; }
    bool hasEquip(EquipSlot s) const { return equip[static_cast<int>(s)] >= 0; }
    bool wounded() const { return hp < maxHp; }
    int handCount() const { return static_cast<int>(hand.size()); }
};

}  // namespace sgs
