#pragma once

#include <string>
#include <vector>

#include "Types.h"

namespace sgs {

// 从 CSV 载入牌堆（数据与逻辑分离，参考 FreeKill 把脚本/数据独立出去的做法）
bool loadDeckFromCsv(const std::string& path, std::vector<CardProto>& out, std::string* err);

// 在常见位置里寻找牌堆数据文件（exe 同级 data/、上级 data/、当前目录）
std::string locateDeckFile();

}  // namespace sgs
