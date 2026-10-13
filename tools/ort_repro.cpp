// 最小 ORT 复现：加载 g0 模型，循环推理 2000 次合成输入。无引擎。
// cl /std:c++17 /EHsc /Isrc ort_repro.cpp /Fe:build/ort_repro.exe
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include "onnxruntime_c_api.h"

static const OrtApi* g = nullptr;

int main(int argc, char** argv) {
    const char* ort_dir = argc > 1 ? argv[1] : "C:/Users/41601/Miniconda3/envs/q35/Lib/site-packages/onnxruntime/capi";
    const char* model = argc > 2 ? argv[2] : "models/g0/model.onnx";
    char old[8192]; GetEnvironmentVariableA("PATH", old, 8192);
    std::string np = std::string(ort_dir) + ";" + old;
    SetEnvironmentVariableA("PATH", np.c_str());
    std::string dll = std::string(ort_dir) + "\\onnxruntime.dll";
    HMODULE h = LoadLibraryA(dll.c_str());
    if (!h) { printf("load fail %lu\n", GetLastError()); return 1; }
    auto fn = (const OrtApiBase* (ORT_API_CALL*)())GetProcAddress(h, "OrtGetApiBase");
    g = fn()->GetApi(ORT_API_VERSION);
    printf("ort version: %s\n", fn()->GetVersionString());

    OrtEnv* env; g->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "repro", &env);
    OrtSessionOptions* so; g->CreateSessionOptions(&so);
    g->SetIntraOpNumThreads(so, 1);
    wchar_t wp[512]; MultiByteToWideChar(CP_UTF8, 0, model, -1, wp, 512);
    OrtSession* ss; g->CreateSession(env, wp, so, &ss);
    printf("session ok\n");

    OrtMemoryInfo* mi; g->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mi);
    std::mt19937 rng(1);
    auto rnd = [&](float* p, size_t n) { for (size_t i = 0; i < n; ++i) p[i] = float(rng() % 7) - 3.0f; };

    const int S = 78, C = 48;
    std::vector<float> scal(24), sattr(S*4), cattr(C*10), mask(C, 1.f);
    std::vector<int64_t> sc(S), cc(C, -1);
    int64_t sh0[2] = {1,24}, sh1[2] = {1,S}, sh2[3] = {1,S,4}, sh3[2] = {1,C}, sh4[3] = {1,C,10};
    for (int it = 0; it < 2000; ++it) {
        rnd(scal.data(), 24); rnd(sattr.data(), sattr.size()); rnd(cattr.data(), cattr.size());
        for (auto& x : sc) x = static_cast<int64_t>(rng() % 31) - 1;
        const char* ins[6] = {"scal","slot_code","slot_attr","cand_code","cand_attr","cand_mask"};
        OrtValue* iv[6]; OrtValue* ov[2] = {nullptr,nullptr};
        auto mk = [&](const void* d, ONNXTensorElementDataType t, const int64_t* sh, int rk, size_t bytes, int slot) {
            OrtStatus* st = g->CreateTensorWithDataAsOrtValue(mi, const_cast<void*>(d), bytes, sh, rk, t, &iv[slot]);
            if (st) { printf("create tensor %d fail: %s\n", slot, g->GetErrorMessage(st)); exit(3); }
        };
        mk(scal.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, sh0, 2, 96, 0);
        mk(sc.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, sh1, 2, S*8, 1);
        mk(sattr.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, sh2, 3, S*4*4, 2);
        mk(cc.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, sh3, 2, C*8, 3);
        mk(cattr.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, sh4, 3, C*10*4, 4);
        mk(mask.data(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, sh3, 2, C*4, 5);
        const char* outs_names[2] = {"logits","value"};
        OrtStatus* st = g->Run(ss, nullptr, ins, iv, 6, outs_names, 2, ov);
        if (st) { printf("run fail: %s\n", g->GetErrorMessage(st)); return 2; }
        float* lg; g->GetTensorMutableData(ov[0], (void**)&lg);
        float best = lg[0]; int bi = 0;
        for (int i = 1; i < C; ++i) if (lg[i] > best) { best = lg[i]; bi = i; }
        g->ReleaseValue(ov[0]); g->ReleaseValue(ov[1]);
        for (int k = 0; k < 6; ++k) g->ReleaseValue(iv[k]);
        if (it % 500 == 0) printf("it=%d best=%d %.4f\n", it, bi, best);
    }
    printf("done 2000 iters ok\n");
    printf("releasing...\n");
    g->ReleaseSession(ss); printf("session rel ok\n");
    g->ReleaseEnv(env); printf("env rel ok\n");
    FreeLibrary(h); printf("dll freed\n");
    return 0;
}
