#pragma once

#include <string>
#include <vector>

#include "Room.h"
#include "Types.h"

namespace sgs {

// 出牌阶段的一个可执行选项
struct PlayOption {
    std::string text;
    CardProto proto;                 // 生效牌型（丈八蛇矛时为虚拟【杀】）
    int cardId = -1;                 // 对应的实体牌（虚拟牌为 -1）
    std::vector<int> cards;          // 支付的牌（虚拟牌在选素材后填充）
    bool virtualCard = false;
    bool needAskTargets = false;
    std::vector<int> presetTargets;  // 无需选目标时（桃园结义等）预先算好
    std::vector<int> targetCandidates;
    int minTargets = 0;
    int maxTargets = 0;
};

// 注册标准版所有牌的效果（装备技能也以事件监听方式注册在这里）
void registerStandardCards(Room& room);

// 构建某角色出牌阶段的所有合法选项
std::vector<PlayOption> buildPlayOptions(Room& room, int who);

// 单目标卡牌效果结算（在无懈、CardEffect 事件之后调用）
void applyCardEffect(Room& room, CardUseStruct& use, int target);

// 某张牌当前是否可以主动使用
bool cardUsableNow(Room& room, int who, const CardProto& proto, int cardId,
                   std::vector<int>& outCandidates, int& outMin, int& outMax, bool& needAsk);

// 本回合【杀】的使用次数上限（诸葛连弩无限）
int slashLimit(const Room& room, int who);

}  // namespace sgs
