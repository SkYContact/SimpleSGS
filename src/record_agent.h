#pragma once

// ============================================================================
// 决策录制：Emitter（二进制落盘）+ RecordingAgent（包装任意 Agent 录其决策）
// ============================================================================
// 二进制格式（小端）：
//   文件头: magic "SGS1" | u32 version=1 | u32 词表n {u16 len + bytes}×n
//           | u32 标签n {u16 len + bytes}×n
//           | u32 F_SCAL | u32 SEAT_N | u32 SLOT_ATTR | u32 CAND_CAP | u32 CAND_ATTR
//   'G' u32 seed                     一局开始
//   'D' u32 self + F_SCAL×f32 scal + SEAT_N×i32 slot_code
//       + SEAT_N×SLOT_ATTR×f32 + CAND_CAP×i32 cand_code
//       + CAND_CAP×CAND_ATTR×f32 + CAND_CAP×f32 mask + i32 action_index
//   'E' i32 winner                   一局结束（-2 平局）
// 布局常量必须与 features.h 一致（文件头携带，Python 侧校验）。
// ============================================================================

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Agent.h"
#include "Types.h"
#include "features.h"

namespace sgs {

class Emitter {
public:
    Emitter(const std::vector<CardProto>& deck, const std::string& path);
    ~Emitter();
    bool ok() const { return out_.good(); }

    void gameStart(unsigned seed);
    void decision(int self, const SgsFeatures& f, int action_index);
    void gameEnd(int winner);

private:
    void writeRaw(const void* p, size_t n) { out_.write(static_cast<const char*>(p), n); }
    template <typename T>
    void writePod(const T& v) { writeRaw(&v, sizeof(T)); }
    void writeStr(const std::string& s);

    std::ofstream out_;
};

// 包装任意 Agent：把它的每次应答翻译成候选下标并写入 Emitter。
// MultiCard 询问按逐张贪心拆成多条决策记录（每次移除已选牌重建候选集）。
class RecordingAgent : public Agent {
public:
    RecordingAgent(std::unique_ptr<Agent> inner, Emitter* em, int seat)
        : inner_(std::move(inner)), em_(em), seat_(seat) {}
    std::string name() const override { return inner_->name(); }
    AskResult ask(Room& room, const Ask& a) override;

private:
    int mapToIndex(const SgsFeatures& f, const Ask& a, const AskResult& r, Room& room) const;
    std::unique_ptr<Agent> inner_;
    Emitter* em_;
    int seat_;
};

}  // namespace sgs
