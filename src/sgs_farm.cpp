#include "sgs_farm.h"

#include <chrono>
#include <atomic>
#include <cstdio>
#include <windows.h>
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

// ---- 决策路径分段埋点（worker CPU 分解，腿末打印）----
namespace fbprof {
using clk = std::chrono::steady_clock;
static long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        clk::now().time_since_epoch()).count();
}
static std::atomic<long long> n_dec{0};
static std::atomic<long long> ns_feat{0}, ns_claim{0}, ns_rows{0}, ns_submit{0},
    ns_argmax{0}, ns_ask_total{0}, ns_multi_loop{0};
static std::atomic<long long> n_game{0};
static std::atomic<long long> ns_newgame{0}, ns_run{0};
static void dump() {
    long long n = n_dec.load();
    if (!n) return;
    auto us = [=](long long v) { return v / 1000.0 / n; };
    std::printf("[fbprof-games] n=%lld newgame=%.0fµs run=%.0fµs/局\n",
                n_game.load(), ns_newgame.load() / 1000.0 / n_game.load(),
                ns_run.load() / 1000.0 / n_game.load());
    std::printf("[fbprof] n=%lld 每决策µs: ask总=%.1f 特征=%.1f claim=%.1f 行拷=%.1f "
                "submit(含挂起)=%.1f argmax=%.1f | multi循环=%.1f\n",
                n, us(ns_ask_total.load()), us(ns_feat.load()), us(ns_claim.load()),
                us(ns_rows.load()), us(ns_submit.load()), us(ns_argmax.load()),
                us(ns_multi_loop.load()));
}
}  // namespace fbprof

namespace {

// 决策级桥 Agent：每次 ask = 一次银行前向（布局契约见 features.h）
class FarmNN : public Agent {
public:
    FarmNN(int seat, std::string name) : seat_(seat), name_(std::move(name)) {}
    std::string name() const override { return name_; }

    AskResult ask(Room& room, const Ask& a) override {
        long long tA = fbprof::nowNs();
        AskResult r;
        if (a.kind == AskKind::MultiCard && a.minCards > 0) {
            Ask rest = a;
            for (int k = 0; k < a.minCards; ++k) {
                long long tM = fbprof::nowNs();
                SgsFeatures f = buildFeatures(room, rest, seat_);
                long long tF = fbprof::nowNs();
                fbprof::ns_feat.fetch_add(tF - tM, std::memory_order_relaxed);
                int idx = forward(f, tA);
                if (idx < 0 || f.cand_mask[idx] <= 0.f) break;
                int cid = f.cand_card_id[idx];
                if (cid < 0) break;
                r.cards.push_back(cid);
                rest.cards.erase(std::remove(rest.cards.begin(), rest.cards.end(), cid),
                                 rest.cards.end());
                fbprof::ns_multi_loop.fetch_add(fbprof::nowNs() - tM,
                                                std::memory_order_relaxed);
                if (rest.cards.empty()) break;
            }
            fbprof::n_dec.fetch_add(0, std::memory_order_relaxed);
            fbprof::ns_ask_total.fetch_add(fbprof::nowNs() - tA,
                                           std::memory_order_relaxed);
            return r;
        }

        SgsFeatures f = buildFeatures(room, a, seat_);
        long long tF = fbprof::nowNs();
        fbprof::ns_feat.fetch_add(tF - tA, std::memory_order_relaxed);
        int idx = forward(f, tA);
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
    // 契约：模型输入 slot_code/cand_code 为 INT64（sgs_export.py torch long），
    // 而 SgsFeatures 持 int32——必须逐元素升宽后再拷入槽行。
    // 血律（2026-10-14 深夜案）：直接 memcpy int32 缓冲按 int64 行宽=每决策
    // 越界读 312B（页界 AV，间歇崩）+ 卡牌编码全垃圾（int32 对被误读为 int64）。
    std::vector<int64_t> slot_i64_, cand_i64_;
    int64_t* slot64(const SgsFeatures& f) {
        slot_i64_.assign(f.slot_code.begin(), f.slot_code.end());
        return slot_i64_.data();
    }
    int64_t* cand64(const SgsFeatures& f) {
        cand_i64_.assign(f.cand_code.begin(), f.cand_code.end());
        return cand_i64_.data();
    }
    int forward(const SgsFeatures& f, long long t_ask0) {
        if (getenv("FARM_PROF_SHELL")) {   // 切片对拍：跳过推理，恒选 0 号候选
            SgsFeatures g; (void)g;
            for (int i = 0; i < CAND_CAP; ++i) if (f.cand_mask[i] > 0.f) return i;
            return -1;
        }
        if (!g_fb_bank) return -1;
        long long t0 = fbprof::nowNs();
        int bk = -1, sl = -1;
        int dev = g_fb_groups >= 2 ? seat_ : -1;   // W2：座位→组
        if (!g_fb_bank->Claim(bk, sl, dev)) return -1;
        long long t1 = fbprof::nowNs();
        auto rowcpy = [&](const char* name, const void* p, size_t bytes) {
            size_t rb = 0;
            void* dst = g_fb_bank->InputRow(bk, sl, name, &rb);
            if (!dst) return;
            std::memcpy(dst, p, bytes < rb ? bytes : rb);
            if (rb > bytes) std::memset((char*)dst + bytes, 0, rb - bytes);
        };
        rowcpy("scal", f.scal, sizeof(float) * F_SCAL);
        rowcpy("slot_code", slot64(f), sizeof(int64_t) * SEAT_N);
        rowcpy("slot_attr", f.slot_attr.data(), sizeof(float) * SEAT_N * SLOT_ATTR);
        rowcpy("cand_code", cand64(f), sizeof(int64_t) * CAND_CAP);
        rowcpy("cand_attr", f.cand_attr.data(), sizeof(float) * CAND_CAP * CAND_ATTR);
        rowcpy("cand_mask", f.cand_mask.data(), sizeof(float) * CAND_CAP);

        long long t2 = fbprof::nowNs();
        float logits[CAND_CAP];
        float value[1];
        inferfarm::OutputDest ds[2];
        ds[0].name = "logits"; ds[0].dst = logits; ds[0].n = CAND_CAP;
        ds[1].name = "value";  ds[1].dst = value;  ds[1].n = 1;
        if (!g_fb_bank->SubmitWait(bk, sl, ds, 2)) return -1;
        long long t3 = fbprof::nowNs();

        int best = -1;
        float bv = -1e30f;
        for (int i = 0; i < CAND_CAP; ++i) {
            if (f.cand_mask[i] > 0.f && logits[i] > bv) { bv = logits[i]; best = i; }
        }
        long long t4 = fbprof::nowNs();
        fbprof::n_dec.fetch_add(1, std::memory_order_relaxed);
        fbprof::ns_claim.fetch_add(t1 - t0, std::memory_order_relaxed);
        fbprof::ns_rows.fetch_add(t2 - t1, std::memory_order_relaxed);
        fbprof::ns_submit.fetch_add(t3 - t2, std::memory_order_relaxed);
        fbprof::ns_argmax.fetch_add(t4 - t3, std::memory_order_relaxed);
        return best;
    }

    int seat_;
    std::string name_;
};

// ---- 穷人采样器：定时挂起工人线程读 RIP，按所属模块直方图（定位 worker CPU 去向）
namespace sampler {
struct Rec { void* mod = nullptr; char name[MAX_PATH] = {0}; long long n = 0; };
static std::atomic<bool> stop{false};

static DWORD WINAPI samplerMain(LPVOID param) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    const inferfarm::Census* cen = (const inferfarm::Census*)param;
    Rec recs[64];
    int nRecs = 0;
    std::printf("[sampler] thread up\n");
    while (cen->tids_worker_n == 0 && !stop.load(std::memory_order_relaxed)) Sleep(5);
    std::printf("[sampler] tids=%d, sampling\n", cen->tids_worker_n);
    while (!stop.load(std::memory_order_relaxed)) {
        for (int i = 0; i < cen->tids_worker_n; ++i) {
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE,
                                  cen->tids_worker[i]);
            if (!h) continue;
            if (SuspendThread(h) != (DWORD)-1) {
                CONTEXT c;
                c.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(h, &c)) {
                    void* rip = (void*)c.Rip;
                    HMODULE mod = nullptr;
                    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCSTR)rip, &mod);
                    int slot = -1;
                    for (int k = 0; k < nRecs; ++k)
                        if (recs[k].mod == mod) { slot = k; break; }
                    if (slot < 0 && nRecs < 64) {
                        slot = nRecs++;
                        recs[slot].mod = mod;
                        if (mod) GetModuleFileNameA(mod, recs[slot].name, MAX_PATH);
                    }
                    if (slot >= 0) recs[slot].n++;
                }
                ResumeThread(h);
            }
            CloseHandle(h);
        }
        Sleep(0);
    }
    long long tot = 0;
    for (int k = 0; k < nRecs; ++k) tot += recs[k].n;
    std::printf("[sampler] done: nRecs=%d tot=%lld\n", nRecs, tot);
    if (tot > 0) {
        std::printf("[sampler] worker RIP samples %lld, by module:\n", tot);
        for (int k = 0; k < nRecs; ++k) {
            if (recs[k].n * 100 < tot) continue;   // <1% 折叠
            char* base = recs[k].name;
            for (char* q = recs[k].name; *q; ++q) if (*q == '/' || *q == 92) base = q + 1;
            std::printf("  %5.1f%%  %s\n", 100.0 * recs[k].n / tot, base);
        }
    }
    return 0;
}
}  // namespace sampler

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
        long long tN = fbprof::nowNs();
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
        fbprof::ns_newgame.fetch_add(fbprof::nowNs() - tN, std::memory_order_relaxed);
    }

    bool AdvanceToDecision() override {
        if (ran_) return false;
        ran_ = true;
        long long tR = fbprof::nowNs();
        winner_ = room_->run();
        fbprof::ns_run.fetch_add(fbprof::nowNs() - tR, std::memory_order_relaxed);
        fbprof::n_game.fetch_add(1, std::memory_order_relaxed);
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
                           int max_turns, const std::string& bagua,
                           const std::string& backend, int slots) {
    FarmLegResult res;
    auto farm = std::make_unique<inferfarm::Farm>();   // sizeof(Farm) 大，走堆
    inferfarm::FarmConfig cfg;
    cfg.name = "sgs";
    cfg.chains = chains;
    cfg.games = games;
    cfg.seed0 = seed0;
    cfg.slots = slots > 0 ? slots : 64;
    cfg.stagger_ms = 0;   // SGS 局短链多：默认 10ms 错峰点火在千链形态=40s 纯睡眠
    // 本机判决（TRT fb256 fixed256 4096 链实测）：自旋组最优，env 未设时给缺省
    if (!getenv("FARM_BANK_SPIN")) _putenv_s("FARM_BANK_SPIN", "1");
    if (!getenv("FARM_CLAIM_SPINS")) _putenv_s("FARM_CLAIM_SPINS", "3000");
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
        d0.model.backend = backend; d0.model.model_path = model_a;
        d0.model.engine_path = model_a;
        d0.model.ort_dir = ort_dir; d0.model.ort_threads = 1; d0.banks = banks > 0 ? banks : 2;
        d1 = d0; d1.model.model_path = model_b; d1.model.engine_path = model_b;
        cfg.devices.push_back(d0);
        cfg.devices.push_back(d1);
        g_fb_groups = 2;
    } else {
        cfg.model.backend = backend;
        cfg.model.model_path = model_a;
        cfg.model.engine_path = model_a;
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

    HANDLE hSampler = nullptr;   // 采样器已弃用（挂 TRT 线程会踩段错误），改切片对拍

    auto thunk = +[](int chain, void* user) -> inferfarm::GameAdapter* {
        return new SgsFarmAdapter((SgsFarmCtx*)user, chain);
    };
    res.sec = farm->RunLeg(thunk, ctx);
    if (hSampler) { sampler::stop.store(true); WaitForSingleObject(hSampler, 3000); CloseHandle(hSampler); sampler::stop.store(false); }
    g_fb_bank = nullptr;
    g_fb_groups = 1;

    const auto& t = farm->tally();
    res.games = t.games_done;
    res.infer_fails = t.infer_fails;
    std::printf("[farm] %d 局用时 %.2fs（%.1f 局/秒）座位0胜 %d/%d 推理失败 %d 决策 %lld 指纹 %016llx\n",
                t.games_done, res.sec, res.sec > 0 ? t.games_done / res.sec : 0.0,
                res.seat0_wins, t.games_done, t.infer_fails,
                (long long)t.decisions, (unsigned long long)t.fingerprint);
    delete ctx;
    fbprof::dump();
    return res;
}

}  // namespace sgs
