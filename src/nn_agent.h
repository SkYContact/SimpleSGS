#pragma once

// ============================================================================
// NNAgent：ONNX (onnxruntime) 候选打分 Agent。
// 模型 = tools/sgs_model.py（PolicyHead4 平移版），输入布局见 features.h。
// ORT DLL 动态加载（LoadLibrary + OrtGetApiBase，照 YGO ai_infer.cpp 的做法），
// 不依赖 oront.lib；DLL 目录用 --ort-dir 指定（缺省 q35 capi）。
// 决策 = 6 输入前向 → logits 对合法候选 argmax → 映射回 AskResult。
// MultiCard 询问按逐张贪心：每选一张移除后重新喂模型。
// ============================================================================

#include <memory>
#include <string>
#include <vector>

#include "Agent.h"

namespace sgs {

class NNAgent : public Agent {
public:
    // model: model.onnx 路径；ort_dir: onnxruntime.dll 所在目录。
    // 返回 null 表示初始化失败（错误已打到 stderr）。
    static std::unique_ptr<NNAgent> create(const std::string& model,
                                           const std::string& ort_dir,
                                           std::string name = "NN");
    ~NNAgent() override;

    std::string name() const override { return name_; }
    AskResult ask(Room& room, const Ask& a) override;

private:
    NNAgent() = default;
    struct Impl;
    Impl* impl_ = nullptr;
    std::string name_;

    int pickIndex(Room& room, const Ask& a, int self);   // 返回候选下标
};

}  // namespace sgs
