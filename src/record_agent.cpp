#include "record_agent.h"

#include <cstdint>
#include <cstring>

#include "Room.h"

namespace sgs {

// ---------------------------------------------------------------- Emitter

Emitter::Emitter(const std::vector<CardProto>& deck, const std::string& path) {
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_.good()) return;
    registerCardVocab(deck);
    writeRaw("SGS1", 4);
    uint32_t ver = 1;
    writePod(ver);
    writePod(static_cast<uint32_t>(cardVocab().size()));
    for (const auto& n : cardVocab()) writeStr(n);
    const auto& tags = tagTable();
    writePod(static_cast<uint32_t>(tags.size()));
    for (const auto* t : tags) writeStr(std::string(t));
    writePod(static_cast<uint32_t>(F_SCAL));
    writePod(static_cast<uint32_t>(SEAT_N));
    writePod(static_cast<uint32_t>(SLOT_ATTR));
    writePod(static_cast<uint32_t>(CAND_CAP));
    writePod(static_cast<uint32_t>(CAND_ATTR));
}

Emitter::~Emitter() { out_.flush(); }

void Emitter::writeStr(const std::string& s) {
    uint16_t n = static_cast<uint16_t>(s.size());
    writePod(n);
    writeRaw(s.data(), n);
}

void Emitter::gameStart(unsigned seed) {
    char tag = 'G';
    writeRaw(&tag, 1);
    writePod(seed);
}

void Emitter::decision(int self, const SgsFeatures& f, int action_index) {
    char tag = 'D';
    writeRaw(&tag, 1);
    int32_t s = self;
    writePod(s);
    writeRaw(f.scal, sizeof(float) * F_SCAL);
    writeRaw(f.slot_code.data(), sizeof(int32_t) * f.slot_code.size());
    writeRaw(f.slot_attr.data(), sizeof(float) * f.slot_attr.size());
    writeRaw(f.cand_code.data(), sizeof(int32_t) * f.cand_code.size());
    writeRaw(f.cand_attr.data(), sizeof(float) * f.cand_attr.size());
    writeRaw(f.cand_mask.data(), sizeof(float) * f.cand_mask.size());
    int32_t a = action_index;
    writePod(a);
}

void Emitter::gameEnd(int winner) {
    char tag = 'E';
    writeRaw(&tag, 1);
    int32_t w = winner;
    writePod(w);
    out_.flush();
}

// -------------------------------------------------------- RecordingAgent

int RecordingAgent::mapToIndex(const SgsFeatures& f, const Ask& a, const AskResult& r,
                               Room& room) const {
    // 内层 Agent 的应答 -> 特征候选下标；映射失败返回 -1（该条不录）
    bool isDecline = false;
    switch (a.kind) {
        case AskKind::Card:
        case AskKind::MultiCard:
            isDecline = r.cards.empty();
            break;
        case AskKind::Option:
            isDecline = r.option < 0;
            break;
        case AskKind::Target:
            isDecline = r.targets.empty();
            break;
    }
    if (isDecline) return f.decline_index;

    for (int i = 0; i < static_cast<int>(f.cand_mask.size()); ++i) {
        if (f.cand_mask[i] <= 0.f) continue;
        switch (a.kind) {
            case AskKind::Card:
            case AskKind::MultiCard:
                if (!r.cards.empty() && f.cand_card_id[i] == r.cards[0]) return i;
                break;
            case AskKind::Option:
                if (f.cand_option_idx[i] == r.option) return i;
                break;
            case AskKind::Target:
                if (!r.targets.empty() && f.cand_target_id[i] == r.targets[0]) return i;
                break;
        }
    }
    return -1;
}

AskResult RecordingAgent::ask(Room& room, const Ask& a) {
    AskResult r = inner_->ask(room, a);

    // MultiCard：按内层实际选出的牌逐张拆记录（每次移除已选重建候选集）
    if (a.kind == AskKind::MultiCard && !r.cards.empty()) {
        Ask rest = a;
        for (int chosen : r.cards) {
            SgsFeatures f = buildFeatures(room, rest, seat_);
            int idx = -1;
            for (int i = 0; i < static_cast<int>(f.cand_mask.size()); ++i) {
                if (f.cand_mask[i] > 0.f && f.cand_card_id[i] == chosen) { idx = i; break; }
            }
            if (idx < 0) break;  // 映射失败，放弃剩余记录
            em_->decision(seat_, f, idx);
            // 从剩余候选里移除这张牌
            rest.cards.erase(std::remove(rest.cards.begin(), rest.cards.end(), chosen),
                             rest.cards.end());
        }
        return r;
    }

    SgsFeatures f = buildFeatures(room, a, seat_);
    int idx = mapToIndex(f, a, r, room);
    if (idx >= 0) em_->decision(seat_, f, idx);
    return r;
}

}  // namespace sgs
