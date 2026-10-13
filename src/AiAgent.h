#pragma once

#include <string>

#include "Agent.h"

namespace sgs {

// 简单启发式 AI：足够跑完一局，也方便做批量自对弈回归
class AiAgent : public Agent {
public:
    explicit AiAgent(std::string name = "电脑") : name_(std::move(name)) {}
    std::string name() const override { return name_; }
    AskResult ask(Room& room, const Ask& a) override;

private:
    AskResult askCard(Room& room, const Ask& a);
    AskResult askMultiCard(Room& room, const Ask& a);
    AskResult askOption(Room& room, const Ask& a);
    AskResult askTarget(Room& room, const Ask& a);

    std::string name_;
};

}  // namespace sgs
