#include "nn_agent.h"

#include <windows.h>

#include <cmath>
#include <numeric>
#include <cstdio>
#include <cstring>

#include "onnxruntime_c_api.h"
#include "Room.h"
#include "features.h"

namespace sgs {

namespace {
const OrtApi* g_ort = nullptr;

void ortCheck(OrtStatus* s, const char* what) {
    if (s) {
        const char* msg = g_ort->GetErrorMessage(s);
        std::fprintf(stderr, "[nn] %s 失败: %s\n", what, msg ? msg : "?");
        g_ort->ReleaseStatus(s);
        std::exit(2);
    }
}
}  // namespace

namespace {
// DLL 进程级单例：g_ort 函数指针指向 DLL 代码页，谁都不能 FreeLibrary
// （否则第二个使用者的调用全部踩已卸载内存）。
HMODULE g_ort_dll = nullptr;
}  // namespace

struct NNAgent::Impl {
    OrtSession* session = nullptr;
    OrtEnv* env = nullptr;
    // 输入输出名（与 sgs_export.py 的 input_names 严格一致）
    const char* in_names[6] = {"scal", "slot_code", "slot_attr",
                               "cand_code", "cand_attr", "cand_mask"};
    const char* out_names[2] = {"logits", "value"};

    ~Impl() {
        // 注意：不 FreeLibrary（g_ort_dll 进程级单例，见文件内注释）
        if (session) g_ort->ReleaseSession(session);
        if (env) g_ort->ReleaseEnv(env);
    }
};

std::unique_ptr<NNAgent> NNAgent::create(const std::string& model,
                                         const std::string& ort_dir,
                                         std::string name) {
    static bool tried = false;
    Impl* im = new Impl();
    do {
        if (!g_ort) {
            if (tried) break;
            tried = true;
            std::string old_path(8192, '\0');
            GetEnvironmentVariableA("PATH", old_path.data(), 8192);
            SetEnvironmentVariableA("PATH", (ort_dir + ";" + old_path.c_str()).c_str());
            std::string dll = ort_dir + "\\onnxruntime.dll";
            g_ort_dll = LoadLibraryA(dll.c_str());
            if (!g_ort_dll) {
                std::fprintf(stderr, "[nn] LoadLibrary %s 失败 GLE=%lu\n", dll.c_str(),
                             GetLastError());
                break;
            }
            auto fn = (const OrtApiBase * (ORT_API_CALL*)())GetProcAddress(g_ort_dll, "OrtGetApiBase");
            if (!fn) { std::fprintf(stderr, "[nn] DLL 无 OrtGetApiBase\n"); break; }
            g_ort = fn()->GetApi(ORT_API_VERSION);
            if (!g_ort) { std::fprintf(stderr, "[nn] GetApi 失败\n"); break; }
        }
        ortCheck(g_ort->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "sgs_nn", &im->env), "CreateEnv");
        OrtSessionOptions* so = nullptr;
        ortCheck(g_ort->CreateSessionOptions(&so), "SessionOptions");
        g_ort->SetIntraOpNumThreads(so, 1);
        g_ort->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL);
        wchar_t wpath[MAX_PATH * 2];
        MultiByteToWideChar(CP_UTF8, 0, model.c_str(), -1, wpath, MAX_PATH * 2);
        OrtStatus* st = g_ort->CreateSession(im->env, wpath, so, &im->session);
        g_ort->ReleaseSessionOptions(so);
        ortCheck(st, "CreateSession");
        auto agent = std::unique_ptr<NNAgent>(new NNAgent());
        agent->impl_ = im;
        agent->name_ = std::move(name);
        return agent;
    } while (false);
    delete im;
    return nullptr;
}

NNAgent::~NNAgent() { delete impl_; }

namespace {

}  // namespace

int NNAgent::pickIndex(Room& room, const Ask& a, int self) {
    SgsFeatures f = buildFeatures(room, a, self);

    static OrtMemoryInfo* minfo = nullptr;
    if (!minfo) {
        ortCheck(g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &minfo),
                 "CreateCpuMemoryInfo");
    }

    std::vector<OrtValue*> inputs;
    std::vector<OrtValue*> owned;
    auto mk = [&](const void* data, ONNXTensorElementDataType type, const int64_t* shape,
                  size_t sn) {
        long long numel = 1;
        for (size_t i = 0; i < sn; ++i) numel *= shape[i];
        OrtValue* ov = nullptr;
        ortCheck(g_ort->CreateTensorWithDataAsOrtValue(
                     minfo, const_cast<void*>(data),
                     static_cast<size_t>(numel) *
                         (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ? 4 : 8),
                     shape, static_cast<int>(sn), type, &ov),
                 "CreateTensor");
        owned.push_back(ov);
        inputs.push_back(ov);
    };

    int64_t sh0[2] = {1, F_SCAL};
    int64_t sh1[2] = {1, SEAT_N};
    int64_t sh2[3] = {1, SEAT_N, SLOT_ATTR};
    int64_t sh3[2] = {1, CAND_CAP};
    int64_t sh4[3] = {1, CAND_CAP, CAND_ATTR};

    mk(f.scal, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, sh0, 2);
    static std::vector<int64_t> slot_i64, cand_i64;   // 单线程使用，保活到 Run
    slot_i64.assign(f.slot_code.begin(), f.slot_code.end());
    cand_i64.assign(f.cand_code.begin(), f.cand_code.end());
    mk(slot_i64.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, sh1, 2);
    mk(f.slot_attr.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, sh2, 3);
    mk(cand_i64.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, sh3, 2);
    mk(f.cand_attr.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, sh4, 3);
    mk(f.cand_mask.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, sh3, 2);

    OrtValue* outs[2] = {nullptr, nullptr};
    ortCheck(g_ort->Run(impl_->session, nullptr, impl_->in_names, inputs.data(), 6,
                        impl_->out_names, 2, outs),
             "Run");

    float* logits = nullptr;
    ortCheck(g_ort->GetTensorMutableData(outs[0], (void**)&logits), "GetTensorData");

    int best = -1;
    float bestv = -1e30f;
    for (int i = 0; i < CAND_CAP; ++i) {
        if (f.cand_mask[i] > 0.f && logits[i] > bestv) {
            bestv = logits[i];
            best = i;
        }
    }
    g_ort->ReleaseValue(outs[0]);
    g_ort->ReleaseValue(outs[1]);
    for (OrtValue* v : owned) g_ort->ReleaseValue(v);
    return best;
}

AskResult NNAgent::ask(Room& room, const Ask& a) {
    int self = a.who;
    AskResult r;

    // MultiCard：逐张贪心（每选一张移除后重喂）
    if (a.kind == AskKind::MultiCard && a.minCards > 0) {
        Ask rest = a;
        for (int k = 0; k < a.minCards; ++k) {
            int idx = pickIndex(room, rest, self);
            int cid = -1;
            {
                SgsFeatures f = buildFeatures(room, rest, self);
                if (idx < 0 || f.cand_mask[idx] <= 0.f) break;
                cid = f.cand_card_id[idx];
            }
            if (cid < 0) break;
            r.cards.push_back(cid);
            rest.cards.erase(std::remove(rest.cards.begin(), rest.cards.end(), cid),
                             rest.cards.end());
            if (rest.cards.empty()) break;
        }
        return r;
    }

    int idx = pickIndex(room, a, self);
    SgsFeatures f = buildFeatures(room, a, self);
    if (idx < 0 || idx >= CAND_CAP || f.cand_mask[idx] <= 0.f) {
        // 模型输出非法 → 按放弃处理（引擎侧调用点有校验兜底）
        return r;
    }
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

}  // namespace sgs
