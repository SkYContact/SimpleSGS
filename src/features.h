#pragma once

// ============================================================================
// 特征契约（唯一真相源，C++ 侧）
// ============================================================================
// 本文件定义 NN 决策点的全部张量布局。tools/sgs_model.py 按这里的常量逐字段
// 同源实现（文件头注释互指），改任何一处必须两边同步改 + 重跑对拍门。
//
// 设计要点：
//  * 每个决策点（Room::askAgent 的每次调用）导出一行组装好的张量，
//    Python 侧只消费张量，不存在 YGO 式 read_bcb 三方装配契约。
//  * 动作空间 = Ask 的合法候选集 + 一个合成的「放弃」候选（allowCancel 时
//    固定追加在末尾），模型输出 = 候选下标（argmax）。
//  * MultiCard 询问（弃牌/贯石斧/蛇矛素材）由 Agent 包装层拆成逐张贪心：
//    每选一张就从候选集里移除再喂一次模型，每次都是一个独立决策记录。
//  * 信息视角：1v1 自对弈研究项目，双方座位都是我们的模型，特征直接取
//    Room 全量权威状态（含对手手牌）——引擎本来就不做信息隐藏。
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include "Types.h"

namespace sgs {

class Room;
struct Ask;

// ---- 布局常量（Python 侧 sgs_model.py 逐字段镜像）----
constexpr int F_SCAL = 24;      // 标量特征维数
constexpr int HAND_CAP = 32;    // 每方手牌槽上限（超出的牌丢弃不编码）
constexpr int EQUIP_N = 4;      // 装备槽（武器/防具/+1马/-1马）
constexpr int JUDGE_N = 3;      // 判定区槽上限
constexpr int SLOT_ATTR = 4;    // 每槽属性维数: suit, rank, area, owner
constexpr int SEAT_N = 2 * (HAND_CAP + EQUIP_N + JUDGE_N);  // 78
constexpr int CAND_CAP = 48;    // 候选上限（含合成「放弃」候选）
constexpr int CAND_ATTR = 10;   // 每候选属性维数，见 CandidateRow

// 座位槽排列顺序：seat0 hand × HAND_CAP, seat0 equip ×4, seat0 judge ×3,
//                 seat1 同理。槽的卡牌编码 = 卡名词表 code（-1 = 空）。
inline int slotBase(int seat) { return seat * (HAND_CAP + EQUIP_N + JUDGE_N); }
inline int slotHand(int seat, int i) { return slotBase(seat) + i; }
inline int slotEquip(int seat, EquipSlot s) { return slotBase(seat) + HAND_CAP + static_cast<int>(s); }
inline int slotJudge(int seat, int i) { return slotBase(seat) + HAND_CAP + EQUIP_N + i; }

// 标量特征排列（F_SCAL 个，顺序固定）：
//  0  turn            当前回合数（从 1 起）
//  1  phase           阶段枚举 Phase 0..6
//  2  self_id         决策方座位 0/1
//  3  my_hp
//  4  my_maxhp
//  5  my_hand_n
//  6  my_slash_used
//  7  my_distance     决策方到对手的距离
//  8  my_range        决策方攻击范围
//  9  in_my_range     对手在我的攻击范围内 0/1
// 10  opp_hp
// 11  opp_maxhp
// 12  opp_hand_n
// 13  opp_slash_used
// 14  opp_range
// 15  in_opp_range    我在对手攻击范围内 0/1
// 16  draw_n          牌堆剩余 /100
// 17  discard_n       弃牌堆 /100
// 18  my_alive        0/1（濒死询问时决策方可能已"死"）
// 19  opp_alive       0/1
// 20  ask_kind        AskKind 0..3
// 21  ask_subject     Ask.subject == self ? 2 : (== opp ? 1 : 0)
// 22  ask_source      Ask.source 同上编码
// 23  nullif_depth    无懈链深度（context=="revert" 时取负深度），其余 0
// 血量/手牌数等原始计数不再归一化（数值域小，交给网络第一层）；
// 只有牌堆/弃牌堆规模除以 100（上面 16/17 两项已注明）。

// 候选行排列（CAND_ATTR = 10 个 float）：
//  0  kind      AskKind 0..3
//  1  tag_code  决策点标签词表 code（tagTable()）
//  2  card_code 卡名词表 code（-1 = 非实体牌，如虚拟杀/纯选项）
//  3  suit      Suit 0..4
//  4  rank      点数/13
//  5  hidden    候选内容不可见（过河拆桥/顺手牵羊指定手牌）
//  6  is_decline 合成「放弃」候选 = 1
//  7  in_my_hand  该实体牌当前在决策方手牌区 0/1
//  8  equip_slot  装备牌槽位 EquipSlot 0..3，非装备 = -1
//  9  target_hp   Target 类候选：该角色的 hp（非 Target = 0）
struct CandidateRow { float a[CAND_ATTR]; };

struct SgsFeatures {
    float scal[F_SCAL];
    std::vector<int32_t> slot_code;                  // SEAT_N，-1 = 空
    std::vector<float> slot_attr;                    // SEAT_N * SLOT_ATTR
    std::vector<int32_t> cand_code;                  // CAND_CAP，-1 = 无
    std::vector<float> cand_attr;                    // CAND_CAP * CAND_ATTR
    std::vector<float> cand_mask;                    // CAND_CAP，1 = 合法动作
    int decline_index = -1;                          // 合成「放弃」候选下标，-1 = 无
    // 候选下标 -> AskResult 的回填所需原料（由 buildFeatures 填好）
    std::vector<int> cand_card_id;                   // Card/MultiCard：候选原始牌 id
    std::vector<int> cand_option_idx;                // Option：候选原始 options 下标
    std::vector<int> cand_target_id;                 // Target：候选原始目标 id

    SgsFeatures();
};

// ---- 词表（固定表，写进 emit 文件头，Python 从文件头读，不硬编码）----
// 卡名词表：由牌堆 CSV 的出现顺序构建（运行时传入），emit 文件头携带。
// 决策点标签词表：固定表 + 兜底 UNKNOWN。
const std::vector<const char*>& tagTable();
int tagCode(const std::string& tag);   // 未收录 → 最后一档 UNKNOWN

// 卡名词表：开局前用牌堆注册一次（emit 文件头会携带词表落盘）
void registerCardVocab(const std::vector<CardProto>& deck);
const std::vector<std::string>& cardVocab();
int nameCode(const std::string& name);
int cardCodeOf(const Room& room, int cardId);

// ---- 特征构建 ----
// self = 决策方座位。把 Ask 的合法候选铺进 CAND_CAP 行，并按 allowCancel
// 追加合成「放弃」候选。返回的 SgsFeatures 同时可用于：
//   * emit（训练语料）
//   * NNAgent 推理输入
//   * 模型输出下标 -> AskResult 的回填（cand_*_id 三张表）
SgsFeatures buildFeatures(const Room& room, const Ask& a, int self);

}  // namespace sgs
