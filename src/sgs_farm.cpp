#include "sgs_farm.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include "inferfarm/bank.h"
#include "inferfarm/farm.h"

#include "CardEffects.h"
#include "Room.h"
#include "features.h"

namespace sgs {

// ============================================================================
// inferfarm 乘客（monolith 形态，同 YGO 回接）：
//   * 整局跑在 AdvanceToDecision 里；决策级经银行 Claim→直写→SubmitWait。
//   * 单模型形态：双座位同一模型（批量自对弈/语料生产）。
//   * 双模型形态（W2 组路由）：组0=model_a 钉座位0，组1=model_b 钉座位1；
//     路由=座位（游戏状态确定函数，满足逐位契约）。
//   * CUDA 内核 run-to-run 有浮点噪声（非逐位）——消费方（演化验收）用
//     双宇宙+门限吃掉，与 YGO 产线同纪律。
// ============================================================================

static inferfarm::BankScheduler* g_fb_bank = nullptr;
static int g_fb_groups = 1;   // 1=单组；2=双组路由（按座位）

namespace {

// 决策级桥 Agent：每次 ask = 一次银行前向（布局契约见 features.h）
class FarmNN : public Agent {
public:
    FarmNN(int seat, std::string name) : seat_(seat), name_(std::move(name)) {}
    std::string name() const override { return name_; }

    AskResult ask(Room& room, const Ask& a) override {
        AskResult r;
        if (a.kind == AskKind::MultiCard && a.minCards > 0) {
            Ask rest = a;
            for (int k = 0; k < a.minCards; ++k) {
                SgsFeatures f = buildFeatures(room, rest, seat_);
                int idx = forward(f);
                if (idx < 0 || f.cand_mask[idx] <= 0.f) break;
                int cid = f.cand_card_id[idx];
                if (cid < 0) break;
                r.cards.push_back(cid);
                rest.cards.erase(std::remove(rest.cards.begin(), rest.cards.end(), cid),
                                 rest.cards.end());
                if (rest.cards.empty()) break;
            }
            return r;
        }

        SgsFeatures f = buildFeatures(room, a, seat_);
        int idx = forward(f);
        if (idx < 0 || idx >= CAND_CAP || f.cand_mask[idx] <= 0.f) return r;  // 放弃
        switch (a.kind) {
            case AskKind::Card:
            case AskKind::MultiCard:
                if (f.cand_card_id[idx] >= 0) r.cards = {f.cand_card_id[idx]};
                break;
            case AskKind::Option:
                if (idx != f.decline_index) r.option = f.cand_option_idx[idx];
                break;
            case AskKind::Target:
                if (f.cand_target_id[idx] >= 0) r.targets = {f.cand_target_id[idx]};
                break;
        }
        return r;
    }

private:
    // 银行前向：argmax 候选下标；失败 -1（=放弃语义，SGS 无"必须答"硬性点）
    int forward(const SgsFeatures& f) {
        if (!g_fb_bank) return -1;
        int bk = -1, sl = -1;
        int dev = g_fb_groups >= 2 ? seat_ : -1;   // W2：座位→组
        if (!g_fb_bank->Claim(bk, sl, dev)) return -1;
        auto rowcpy = [&](const char* name, const void* p, size_t bytes) {
            size_t rb = 0;
            void* dst = g_fb_bank->InputRow(bk, sl, name, &rb);
            if (!dst) return;
            std::memcpy(dst, p, bytes < rb ? bytes : rb);
            if (rb > bytes) std::memset((char*)dst + bytes, 0, rb - bytes);
        };
        rowcpy("scal", f.scal, sizeof(float) * F_SCAL);
        rowcpy("slot_code", f.slot_code.data(), sizeof(int64_t) * SEAT_N);
        rowcpy("slot_attr", f.slot_attr.data(), sizeof(float) * SEAT_N * SLOT_ATTR);
        rowcpy("cand_code", f.cand_code.data(), sizeof(int64_t) * CAND_CAP);
        rowcpy("cand_attr", f.cand_attr.data(), sizeof(float) * CAND_CAP * CAND_ATTR);
        rowcpy("cand_mask", f.cand_mask.data(), sizeof(float) * CAND_CAP);

        float logits[CAND_CAP];
        float value[1];
        inferfarm::OutputDest ds[2];
        ds[0].name = "logits"; ds[0].dst = logits; ds[0].n = CAND_CAP;
        ds[1].name = "value";  ds[1].dst = value;  ds[1].n = 1;
        if (!g_fb_bank->SubmitWait(bk, sl, ds, 2)) return -1;

        int best = -1;
        float bv = -1e30f;
        for (int i = 0; i < CAND_CAP; ++i) {
            if (f.cand_mask[i] > 0.f && logits[i] > bv) { bv = logits[i]; best = i; }
        }
        return best;
    }

    int seat_;
    std::string name_;
};

struct SgsFarmCtx {
    std::vector<CardProto> deck;
    int games = 64;
    int chains = 8;
    uint32_t seed0 = 4242;
    int hp = 4;
    int max_turns = 200;
    EightDiagramMode bagua = EightDiagramMode::Ask;
    int* seat0_wins = nullptr;   // 收账（适配器析构时不可用，直接原子加到这里）
};

// monolith 适配器：整局跑在 AdvanceToDecision 里
class SgsFarmAdapter final : public inferfarm::GameAdapter {
public:
    SgsFarmAdapter(SgsFarmCtx* ctx, int chain) : ctx_(ctx), chain_(chain) {}

    void NewGame(uint64_t seed, bool we_first) override {
        room_ = std::make_unique<Room>(ctx_->deck, (unsigned)seed);
        room_->setSilent(true);
        room_->setMaxTurns(ctx_->max_turns);
        room_->setEightDiagramMode(ctx_->bagua);
        registerStandardCards(*room_);
        room_->setupPlayer(0, "seat0", true, ctx_->hp, std::make_unique<FarmNN>(0, "m0"));
        room_->setupPlayer(1, "seat1", false, ctx_->hp, std::make_unique<FarmNN>(1, "m1"));
        we_first_ = we_first;
        ran_ = false;
        winner_ = -1;
    }

    bool AdvanceToDecision() override {
        if (ran_) return false;
        ran_ = true;
        winner_ = room_->run();
        if (winner_ == 0 && ctx_->seat0_wins) (*ctx_->seat0_wins)++;
        return false;
    }

    void AssembleInto(inferfarm::SlotWriter&) override {}
    int CollectOutputs(inferfarm::OutputDest*, int) override { return 0; }
    void ApplyResult() override {}
    void OnInferFail() override {}

    bool IsDone() override { return ran_; }
    int Outcome() override {
        if (winner_ < 0) return -1;
        return winner_ == 0 ? 1 : 0;   // "we" = 座位0（单模型=先手位；双模型=cand）
    }
    bool WeAreFirst() override { return we_first_; }
    long long GameFingerprint() override {
        return (long long)(winner_ + 2) * 1000000LL + room_->turnCount();
    }
    inferfarm::ITlsFrame* TlsFrame() override { return nullptr; }

private:
    SgsFarmCtx* ctx_;
    int chain_;
    std::unique_ptr<Room> room_;
    bool ran_ = false;
    bool we_first_ = true;
    int winner_ = -1;
};

}  // namespace

FarmLegResult sgs_farm_run(const std::vector<CardProto>& deck, const std::string& model_a,
                           const std::string& model_b, const std::string& ort_dir,
                           int chains, int games, unsigned seed0, int banks, int hp,
                           int max_turns, const std::string& bagua) {
    FarmLegResult res;
    auto farm = std::make_unique<inferfarm::Farm>();   // sizeof(Farm) 大，走堆
    inferfarm::FarmConfig cfg;
    cfg.name = "sgs";
    cfg.chains = chains;
    cfg.games = games;
    cfg.seed0 = seed0;
    cfg.slots = 64;
    if (banks > 0) cfg.banks = banks;

    SgsFarmCtx* ctx = new SgsFarmCtx();
    ctx->deck = deck;
    ctx->games = games;
    ctx->chains = chains;
    ctx->seed0 = seed0;
    ctx->hp = hp;
    ctx->max_turns = max_turns;
    parseEightDiagramMode(bagua, ctx->bagua);
    ctx->seat0_wins = &res.seat0_wins;

    if (!model_b.empty()) {
        // W2 双组：组0=model_a（座位0），组1=model_b（座位1）
        inferfarm::DeviceConfig d0, d1;
        d0.model.backend = "ort"; d0.model.model_path = model_a;
        d0.model.ort_dir = ort_dir; d0.model.ort_threads = 1; d0.banks = banks > 0 ? banks : 2;
        d1 = d0; d1.model.model_path = model_b;
        cfg.devices.push_back(d0);
        cfg.devices.push_back(d1);
        g_fb_groups = 2;
    } else {
        cfg.model.backend = "ort";
        cfg.model.model_path = model_a;
        cfg.model.ort_dir = ort_dir;
        cfg.model.ort_threads = 1;
        g_fb_groups = 1;
    }

    if (!farm->Init(cfg)) {
        std::fprintf(stderr, "[farm] Init 失败\n");
        delete ctx;
        g_fb_groups = 1;
        return res;
    }
    g_fb_bank = farm->bank();

    auto thunk = +[](int chain, void* user) -> inferfarm::GameAdapter* {
        return new SgsFarmAdapter((SgsFarmCtx*)user, chain);
    };
    res.sec = farm->RunLeg(thunk, ctx);
    g_fb_bank = nullptr;
    g_fb_groups = 1;

    const auto& t = farm->tally();
    res.games = t.games_done;
    res.infer_fails = t.infer_fails;
    std::printf("[farm] %d 局用时 %.2fs（%.1f 局/秒）座位0胜 %d/%d 推理失败 %d\n",
                t.games_done, res.sec, res.sec > 0 ? t.games_done / res.sec : 0.0,
                res.seat0_wins, t.games_done, t.infer_fails);
    delete ctx;
    return res;
}

}  // namespace sgs
