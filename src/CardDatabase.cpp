#include "CardDatabase.h"

#include <fstream>

#include "Util.h"

namespace sgs {

namespace {

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r' && c != '\n') {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

bool parseSuit(const std::string& s, Suit& out) {
    if (s == "S") { out = Suit::Spade; return true; }
    if (s == "H") { out = Suit::Heart; return true; }
    if (s == "C") { out = Suit::Club; return true; }
    if (s == "D") { out = Suit::Diamond; return true; }
    return false;
}

bool parseType(const std::string& s, CardType& out) {
    if (s == "basic") { out = CardType::Basic; return true; }
    if (s == "trick") { out = CardType::Trick; return true; }
    if (s == "delayed") { out = CardType::DelayedTrick; return true; }
    if (s == "equip") { out = CardType::Equip; return true; }
    return false;
}

bool parseSlot(const std::string& s, EquipSlot& out) {
    if (s.empty()) { out = EquipSlot::None; return true; }
    if (s == "weapon") { out = EquipSlot::Weapon; return true; }
    if (s == "armor") { out = EquipSlot::Armor; return true; }
    if (s == "horse_def") { out = EquipSlot::DefensiveHorse; return true; }
    if (s == "horse_off") { out = EquipSlot::OffensiveHorse; return true; }
    return false;
}

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

}  // namespace

bool loadDeckFromCsv(const std::string& path, std::vector<CardProto>& out, std::string* err) {
    std::ifstream in(path);
    if (!in) {
        if (err) *err = str("无法打开牌堆文件: ", path);
        return false;
    }
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        auto f = split(t, ',');
        if (f.size() < 6) {
            if (err) *err = str(path, ":", lineNo, " 字段数不足");
            return false;
        }
        for (auto& x : f) x = trim(x);
        while (f.size() < 7) f.push_back("");

        CardProto p;
        p.name = f[0];
        p.display = f[1];
        if (!parseType(f[2], p.type)) {
            if (err) *err = str(path, ":", lineNo, " 未知牌类型 ", f[2]);
            return false;
        }
        if (!parseSlot(f[3], p.slot)) {
            if (err) *err = str(path, ":", lineNo, " 未知装备槽 ", f[3]);
            return false;
        }
        if (!parseSuit(f[4], p.suit)) {
            if (err) *err = str(path, ":", lineNo, " 未知花色 ", f[4]);
            return false;
        }
        p.rank = toInt(f[5], 1);
        p.range = toInt(f[6], 0);
        if (p.type == CardType::Equip && p.slot == EquipSlot::None) {
            if (err) *err = str(path, ":", lineNo, " 装备牌缺少 slot");
            return false;
        }
        out.push_back(p);
    }
    if (out.empty()) {
        if (err) *err = str(path, " 牌堆为空");
        return false;
    }
    return true;
}

std::string locateDeckFile() {
    const std::vector<std::string> candidates = {
        "data/standard_cards.csv",
        "../data/standard_cards.csv",
        "../../data/standard_cards.csv",
        "standard_cards.csv",
        "../data/standard_cards.csv",
    };
    for (const auto& c : candidates) {
        if (fileExists(c)) return c;
    }
    return "";
}

}  // namespace sgs
