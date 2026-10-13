#pragma once

#include <string>

#include "Agent.h"

namespace sgs {

// 人工操作：在控制台里读输入（数字选项），方便调试单局
class ConsoleAgent : public Agent {
public:
    explicit ConsoleAgent(std::string name = "你") : name_(std::move(name)) {}
    std::string name() const override { return name_; }
    AskResult ask(Room& room, const Ask& a) override;

private:
    void printStatus(Room& room, int who) const;

    std::string name_;
};

}  // namespace sgs
