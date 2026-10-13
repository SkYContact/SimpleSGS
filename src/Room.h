#pragma once

#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Agent.h"
#include "Event.h"
#include "Player.h"
#include "Types.h"

namespace sgs {

struct CardLoc {
    Area area = Area::None;
    int owner = -1;
};

// 房间 = 服务器权威的规则内核（单线程）。
// 结构借鉴 QSanguosha 的 Room：所有牌/角色状态都在这里，玩家只能通过 Agent 应答。
class Room {
public:
    Room(std::vector<CardProto> protos, unsigned seed);

    void setupPlayer(int who, const std::string& name, bool male, int maxHp,
                     std::unique_ptr<Agent> agent);

    // ---------------------------------------------------------- 查询
    Player& player(int id) { return *players_[id]; }
    const Player& player(int id) const { return *players_[id]; }
    const Card& card(int id) const { return cards_[id]; }
    std::string playerName(int id) const;

    int playerCount() const { return static_cast<int>(players_.size()); }
    std::vector<int> alivePlayers() const;
    int aliveCount() const;
    int other(int id) const;                        // 1v1：唯一的对手

    int distance(int from, int to) const;
    int attackRange(int who) const;
    bool inAttackRange(int from, int to) const;

    int equipId(int who, EquipSlot slot) const;
    bool hasEquipNamed(int who, EquipSlot slot, const char* name) const;
    bool hasEquipNamed(int who, const char* name) const;

    // ---------------------------------------------------------- 牌区
    Area areaOf(int cardId) const;
    int ownerOf(int cardId) const;
    std::vector<int> handOf(int owner) const { return player(owner).hand; }
    std::vector<int> equipsOf(int owner) const;
    std::vector<int> judgeZoneOf(int owner) const { return player(owner).judgeZone; }
    std::vector<int> allCardsOf(int owner) const;    // 手牌 + 装备 + 判定
    std::vector<int> handCardsNamed(int owner, const char* name) const;
    bool hasCardNamed(int owner, const char* name) const;

    // ---------------------------------------------------------- 牌移动
    void removeCard(int cardId);
    void addCard(int cardId, int owner, Area area);
    void moveCards(const std::vector<int>& ids, int toOwner, Area toArea, const std::string& reason);
    void discardCards(const std::vector<int>& ids, const std::string& reason);
    std::vector<int> drawCards(int who, int n);
    // 从牌堆顶亮出 n 张牌（置为"离场"状态，由调用方决定归属）
    std::vector<int> revealCards(int n);
    void equipCard(int who, int cardId);
    void putJudgeCard(int owner, int cardId);

    // ---------------------------------------------------------- 结算
    bool useCard(CardUseStruct& use);
    bool askNullification(const CardUseStruct& use, int target);
    int judge(int who, const std::string& reason, JudgeStruct* out = nullptr);
    void processJudgeZone(int who);
    bool damage(DamageStruct dmg);
    void recover(int who, int amount);
    void loseHp(int who, int amount);
    void enterDying(int who, int killer);
    void killPlayer(int who, int killer);
    void checkGameOver();

    // ---------------------------------------------------------- 事件
    void registerTrigger(const std::string& name, Event e, int owner, int priority,
                         std::function<bool(Room&, TriggerParams&)> fn);
    bool trigger(Event e, TriggerParams& p);
    void clearTriggersNamed(const std::string& name);

    // ---------------------------------------------------------- 询问
    AskResult askAgent(const Ask& a);
    // 询问 who 打出一张名为 cardName 的牌；返回 -1 未响应 / >=0 打出的牌 id / kVirtualCard 虚拟响应
    int askForResponse(int who, const char* cardName, const std::string& prompt, int source = -1);
    std::vector<int> askTargets(int user, const std::string& cardDisplay,
                                const std::vector<int>& candidates, int minT, int maxT);

    // ---------------------------------------------------------- 输出
    void chat(const std::string& s);
    const std::vector<std::string>& history() const { return log_; }
    std::string cardLabel(int cardId) const;
    std::string useLabel(const CardUseStruct& use) const;

    // ---------------------------------------------------------- 主循环
    int run();
    void runTurn(int who);
    void runDrawPhase(int who);
    void runPlayPhase(int who);
    void runDiscardPhase(int who);

    bool gameOver() const { return winner_ != -1; }
    int winner() const { return winner_; }
    int turnCount() const { return turn_; }
    int currentPlayer() const { return current_; }
    std::mt19937& rng() { return rng_; }
    void setMaxTurns(int n) { maxTurns_ = n; }
    void setSilent(bool s) { silent_ = s; }  // 跑批/网页时不打印到 stdout
    // 【八卦阵】发动方式（默认自动：没手牌【闪】时才判定）
    void setEightDiagramMode(EightDiagramMode m) { eightDiagramMode_ = m; }
    // 动态来源：网页上"设置"里可以随时改。设了它就以它为准（返回 0/1/2）。
    // 为什么用回调而不是直接调 setter：wasm 挂起等玩家点击时不能反调进 wasm，
    // 所以由 JS 写全局变量、C++ 在需要判定的那一刻现读。
    void setEightDiagramModeProvider(std::function<int()> f) { baguaModeProvider_ = std::move(f); }
    EightDiagramMode eightDiagramMode() const;
    // 日志外送（WebAssembly / 录像 / 网络都用它）
    void setLogSink(std::function<void(const std::string&)> fn) { logSink_ = std::move(fn); }
    int drawPileSize() const { return static_cast<int>(drawPile_.size()); }
    int discardPileSize() const { return static_cast<int>(discardPile_.size()); }
    // 牌数守恒自检：所有区域的牌加起来应该等于整副牌（少了就说明有牌被"吃掉"了）
    int totalCardsTracked() const;
    int deckSize() const { return static_cast<int>(cards_.size()); }

private:
    void shuffle(std::vector<int>& pile);
    void reshuffle();
    void clearPlayerCards(int who);
    void announceUse(const CardUseStruct& use);

    std::vector<std::unique_ptr<Player>> players_;
    std::vector<Card> cards_;
    std::vector<CardLoc> loc_;
    std::vector<int> drawPile_;
    std::vector<int> discardPile_;
    std::vector<int> revealedPile_;  // 亮牌区（五谷丰登等）
    std::vector<std::shared_ptr<Trigger>> triggers_;
    std::vector<std::string> log_;
    std::mt19937 rng_;
    int current_ = -1;
    int turn_ = 0;
    int winner_ = -1;   // -1 未结束；-2 平局；>=0 获胜者
    int maxTurns_ = 200;
    bool silent_ = false;
    EightDiagramMode eightDiagramMode_ = EightDiagramMode::Auto;
    std::function<int()> baguaModeProvider_;
    std::function<void(const std::string&)> logSink_;
};

}  // namespace sgs
