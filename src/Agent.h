#pragma once

#include <string>
#include <vector>

#include "Types.h"

namespace sgs {

class Room;

// 询问种类：把"规则结算"和"玩家决策"解耦（借鉴 FreeKill 的请求-应答模型）
enum class AskKind {
    Card,        // 选一张牌（响应【闪】、【桃】、【无懈可击】等）
    MultiCard,   // 选多张牌（弃牌、丈八蛇矛素材）
    Option,      // 从候选选项中选一个（出牌阶段、五谷丰登选牌、雌雄双股剑）
    Target       // 选目标角色
};

struct Ask {
    AskKind kind = AskKind::Card;
    std::string tag;        // 语义标签: Jink / Slash / Peach / Nullification / Play / Discard / Harvest ...
    std::string prompt;     // 展示给玩家的说明
    int who = -1;           // 被询问者
    int subject = -1;       // 相关角色（濒死被救者、被无懈的目标等）
    int source = -1;        // 相关牌的使用者（无懈可击：被无懈的那张牌是谁用的）
    std::string context;    // 附加语义，如无懈的 "cancel" / "revert"

    std::vector<int> cards;                  // 候选实体牌 id
    std::vector<std::string> options;        // 候选选项文本
    std::vector<int> optionCards;            // 与 options 平行：该选项对应的牌（-1 表示虚拟牌/无牌）
    std::vector<int> targetCandidates;       // 候选目标角色

    int minCards = 0, maxCards = 0;
    int minTargets = 0, maxTargets = 0;
    bool hidden = false;                     // 候选牌内容不可见（如【过河拆桥】看对方手牌）
    bool allowCancel = true;
};

struct AskResult {
    int option = -1;                 // 选中的选项下标，-1 = 放弃
    std::vector<int> cards;          // 选中的牌
    std::vector<int> targets;        // 选中的目标
};

class Agent {
public:
    virtual ~Agent() = default;
    virtual std::string name() const = 0;
    virtual AskResult ask(Room& room, const Ask& a) = 0;
};

}  // namespace sgs
