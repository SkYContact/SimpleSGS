#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace sgs {

// 简易字符串拼接（替代 std::format / fmt，C++17 可用）
template <class... Args>
std::string str(Args&&... args) {
    std::ostringstream os;
    (os << ... << args);
    return os.str();
}

inline void eraseValue(std::vector<int>& v, int x) {
    v.erase(std::remove(v.begin(), v.end(), x), v.end());
}

inline bool contains(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

// 不抛异常的字符串转整数（WebAssembly 下异常被禁用，std::stoi 失败会 abort）
inline int toInt(const std::string& s, int def = 0) {
    size_t i = 0;
    bool neg = false;
    int v = 0;
    bool any = false;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        neg = (s[i] == '-');
        ++i;
    }
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
        v = v * 10 + (s[i] - '0');
        any = true;
    }
    if (!any) return def;
    return neg ? -v : v;
}

// 解析 "1,2,3" / "1 2 3" 形式的 id 列表
inline std::vector<int> parseIntList(const std::string& s) {
    std::vector<int> out;
    std::string cur;
    for (char c : s + ",") {
        if (c >= '0' && c <= '9') {
            cur.push_back(c);
        } else if (c == '-' && cur.empty()) {
            cur.push_back(c);
        } else if (!cur.empty()) {
            out.push_back(toInt(cur, -1));
            cur.clear();
        }
    }
    return out;
}

}  // namespace sgs
