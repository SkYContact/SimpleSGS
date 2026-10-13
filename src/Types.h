#pragma once

#include <string>
#include <vector>

namespace sgs {

// ---------------------------------------------------------------- 基础枚举
enum class Suit { Spade = 0, Heart, Club, Diamond, None };
enum class CardType { Basic, Trick, DelayedTrick, Equip };
enum class EquipSlot { None = -1, Weapon = 0, Armor = 1, DefensiveHorse = 2, OffensiveHorse = 3 };
enum class Phase { None = 0, RoundStart, Judge, Draw, Play, Discard, RoundEnd };
enum class DamageNature { Normal = 0, Fire, Thunder };
enum class Area { None = 0, Hand, Equip, Judge, DrawPile, DiscardPile };

// 单回合【杀】的使用次数上限（无武将、无技能时的基础值）
constexpr int kBaseSlashLimit = 1;
constexpr int kUnlimited = 999;
// 起始手牌数（无武将，双方相同）
constexpr int kInitHand = 4;

// askForResponse 的返回值语义：-1 = 未响应；>=0 = 真正打出了这张牌；-2 = 视为响应（虚拟牌，如八卦阵）
constexpr int kVirtualCard = -2;
inline bool responseOk(int r) { return r >= 0 || r == kVirtualCard; }

// ---------------------------------------------------------------- 牌名常量
// 用英文标识做逻辑 key，展示名放在数据表里（便于以后做本地化）
namespace CardName {
inline constexpr const char* Slash = "Slash";                              // 杀
inline constexpr const char* Jink = "Jink";                                // 闪
inline constexpr const char* Peach = "Peach";                              // 桃
inline constexpr const char* Dismantlement = "Dismantlement";              // 过河拆桥
inline constexpr const char* Snatch = "Snatch";                            // 顺手牵羊
inline constexpr const char* ExNihilo = "ExNihilo";                        // 无中生有
inline constexpr const char* Duel = "Duel";                                // 决斗
inline constexpr const char* Collateral = "Collateral";                    // 借刀杀人
inline constexpr const char* PeachGarden = "PeachGarden";                  // 桃园结义
inline constexpr const char* BarbarianInvasion = "BarbarianInvasion";      // 南蛮入侵
inline constexpr const char* ArrowBarrage = "ArrowBarrage";                // 万箭齐发
inline constexpr const char* Harvest = "Harvest";                          // 五谷丰登
inline constexpr const char* Nullification = "Nullification";              // 无懈可击
inline constexpr const char* Indulgence = "Indulgence";                    // 乐不思蜀
inline constexpr const char* Lightning = "Lightning";                      // 闪电
inline constexpr const char* ZhugeCrossbow = "ZhugeCrossbow";              // 诸葛连弩
inline constexpr const char* QinggangSword = "QinggangSword";              // 青釭剑
inline constexpr const char* DoubleSword = "DoubleSword";                  // 雌雄双股剑
inline constexpr const char* GreenDragonBlade = "GreenDragonBlade";        // 青龙偃月刀
inline constexpr const char* Spear = "Spear";                              // 丈八蛇矛
inline constexpr const char* Axe = "Axe";                                  // 贯石斧
inline constexpr const char* Halberd = "Halberd";                          // 方天画戟
inline constexpr const char* KylinBow = "KylinBow";                        // 麒麟弓
inline constexpr const char* EightDiagram = "EightDiagram";                // 八卦阵
inline constexpr const char* RenwangShield = "RenwangShield";              // 仁王盾
}  // namespace CardName

// ---------------------------------------------------------------- 牌
// 卡牌原型 = 数据表的一行
struct CardProto {
    std::string name;                 // 逻辑标识，如 "Slash"
    std::string display;              // 显示名，如 "杀"
    CardType type = CardType::Basic;
    EquipSlot slot = EquipSlot::None; // 装备槽（仅装备牌有效）
    Suit suit = Suit::None;           // 虚拟牌为 None（无色）
    int rank = 0;                     // 1..13
    int range = 0;                    // 武器攻击范围
};

// 一张具体的牌（牌池里的对象，容器只保存 id）
struct Card {
    int id = -1;
    CardProto proto;
    bool virtualCard = false;   // 由技能转化而来的虚拟牌，不对应实体牌
    int playedBy = -1;          // 延时锦囊的原使用者（判定阶段要用）

    const std::string& name() const { return proto.name; }
    const std::string& display() const { return proto.display; }
};

// 一次"使用牌"的结算描述（QSanguosha 的 CardUseStruct 思路）
struct CardUseStruct {
    int user = -1;                 // 使用者
    int cardId = -1;               // 主牌（虚拟牌为 -1）
    std::vector<int> cards;        // 实际支付的实体牌（丈八蛇矛时为 2 张素材）
    CardProto proto;               // 生效牌型（虚拟牌时与 cardId 的牌型不同）
    bool virtualCard = false;
    std::vector<int> targets;      // 目标（可在 CardUsed 事件里被修改，如方天画戟）
    std::vector<int> sharedCards;  // 跨目标共享的临时牌（五谷丰登亮出的牌）
};

struct DamageStruct {
    int from = -1;                     // -1 表示无来源（闪电）
    int to = -1;
    int cardId = -1;
    int amount = 1;
    DamageNature nature = DamageNature::Normal;
    bool fromCard = true;              // 是否由牌造成
};

struct JudgeStruct {
    int who = -1;
    int cardId = -1;
    std::string reason;
    std::vector<int> retrialCards;     // 改判牌（本引擎无武将，留作扩展）
};

// ---------------------------------------------------------------- 名称工具
const char* suitName(Suit s);       // 黑桃 / 红桃 / 梅花 / 方块 / 无色
const char* suitSymbol(Suit s);     // ♠ ♥ ♣ ♦
const char* typeName(CardType t);
const char* phaseName(Phase p);
const char* areaName(Area a);
const char* natureName(DamageNature n);

bool isRed(Suit s);
bool isBlack(Suit s);

std::string cardLabel(const CardProto& proto, bool virtualCard = false);
std::string rankLabel(int rank);

}  // namespace sgs
