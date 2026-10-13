#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Types.h"

namespace sgs {

class Room;

// 事件表：卡牌/装备的效果都以"监听事件"的方式注册，
// 以后要加武将技能，只需再注册一批监听函数，内核不用改。
enum class Event {
    GameStart = 0,
    TurnStart,
    TurnEnd,
    PhaseStart,
    PhaseEnd,
    Judge,          // 判定
    CardUsed,       // 声明使用一张牌（可修改目标）
    CardEffect,     // 对某个目标生效前（可取消）
    SlashDodged,    // 【杀】被【闪】抵消（贯石斧 / 青龙偃月刀）
    Damage,         // 伤害结算前
    Damaged,        // 伤害结算后
    Dying,          // 进入濒死
    Die,            // 阵亡
    Recover,        // 回复体力
    AskForCard,     // 需要打出某张牌时（八卦阵可提供替代）
    CardResponded,  // 实体响应牌已支付（供展示层观察）
    CardsMoved,     // 牌区更新完成（供展示层观察）
    CardFinished,   // 一次使用结算完成
    Count
};

struct CardMove {
    int cardId = -1;
    int fromOwner = -1;
    Area fromArea = Area::None;
};

struct TriggerParams {
    Event event = Event::Count;
    int who = -1;              // 事件主体
    int source = -1;           // 事件来源
    int target = -1;           // 单目标结算时的目标
    int cardId = -1;

    CardUseStruct* use = nullptr;
    DamageStruct* damage = nullptr;
    JudgeStruct* judge = nullptr;

    std::string askName;              // AskForCard: 需要的牌名
    std::string reason;               // 文本说明
    std::vector<int> askCandidates;   // AskForCard: 被询问者手上可用的实体牌
    bool provided = false;            // AskForCard: 已提供替代响应
    int providedCard = -1;

    bool forcedHit = false;           // SlashDodged: 强制命中
    bool effectCancelled = false;     // CardEffect: 该目标效果被取消
    int number = 0;                   // 通用数值
    std::vector<CardMove> moves;      // CardsMoved: 实体牌及移动前的位置
    Area toArea = Area::None;
};

// 触发器：名字 + 监听的事件 + 优先级（越小越先）+ 拥有者（-1 = 全局）
struct Trigger {
    std::string name;
    Event event = Event::Count;
    int priority = 0;
    int owner = -1;
    std::function<bool(Room&, TriggerParams&)> fn;  // 返回 false 中断本次事件分发
};

}  // namespace sgs
