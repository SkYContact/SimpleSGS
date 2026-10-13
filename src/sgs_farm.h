#pragma once

// ============================================================================
// inferfarm 乘客接入（--farm）：monolith 适配器 + 决策级银行批量。
// 单模型形态 = 批量自对弈；双模型形态（model_b 非空）= W2 组路由
#//(组0=model_a 坐座位0，组1=model_b 坐座位1)，演化配对验收腿用。
// 详见 sgs_farm.cpp 文件头注释。
// ============================================================================

#include <string>
#include <vector>

#include "Types.h"

namespace inferfarm {
class Farm;
}

namespace sgs {

struct FarmLegResult {
    double sec = 0;
    int games = 0;        // 总局数
    int seat0_wins = 0;   // 座位0（单模型=先手位视角；双模型=cand）胜局
    int infer_fails = 0;
};

// 跑一条腿。banks<=0 用缺省 2；model_b 非空启用双组路由（两模型都须 fb 烤制）。
FarmLegResult sgs_farm_run(const std::vector<CardProto>& deck, const std::string& model_a,
                           const std::string& model_b, const std::string& ort_dir,
                           int chains, int games, unsigned seed0, int banks, int hp,
                           int max_turns, const std::string& bagua);

}  // namespace sgs
