#include "Room.h"

#include <algorithm>
#include <iostream>

#include "CardEffects.h"
#include "Util.h"

namespace sgs {

// ============================================================ 构造 / 初始化

Room::Room(std::vector<CardProto> protos, unsigned seed) : rng_(seed) {
    for (auto& p : protos) {
        Card c;
        c.id = static_cast<int>(cards_.size());
        c.proto = p;
        cards_.push_back(c);
        loc_.push_back(CardLoc{Area::DrawPile, -1});
        drawPile_.push_back(c.id);
    }
    shuffle(drawPile_);
}

void Room::setupPlayer(int who, const std::string& name, bool male, int maxHp,
                       std::unique_ptr<Agent> agent) {
    while (static_cast<int>(players_.size()) <= who) {
        players_.push_back(std::make_unique<Player>());
    }
    Player& p = *players_[who];
    p.id = who;
    p.name = name;
    p.male = male;
    p.hp = maxHp;
    p.maxHp = maxHp;
    p.agent = std::move(agent);
}

std::string Room::playerName(int id) const {
    if (id < 0) return "无来源";
    return players_[id]->name;
}

// 【八卦阵】发动方式：有动态来源（网页设置）就现读，否则用静态设置
EightDiagramMode Room::eightDiagramMode() const {
    if (baguaModeProvider_) return eightDiagramModeFromInt(baguaModeProvider_());
    return eightDiagramMode_;
}

// ============================================================ 查询

std::vector<int> Room::alivePlayers() const {
    std::vector<int> out;
    for (auto& p : players_) {
        if (p->alive) out.push_back(p->id);
    }
    return out;
}

int Room::aliveCount() const {
    int n = 0;
    for (auto& p : players_) {
        if (p->alive) ++n;
    }
    return n;
}

int Room::other(int id) const {
    for (auto& p : players_) {
        if (p->id != id && p->alive) return p->id;
    }
    return id == 0 ? 1 : 0;
}

// 1v1 恒定相邻；防守马(+1) 拉开距离，进攻马(-1) 拉近
int Room::distance(int from, int to) const {
    if (from == to) return 0;
    int d = 1;
    if (player(to).hasEquip(EquipSlot::DefensiveHorse)) ++d;
    if (player(from).hasEquip(EquipSlot::OffensiveHorse)) --d;
    return std::max(1, d);
}

int Room::attackRange(int who) const {
    int w = equipId(who, EquipSlot::Weapon);
    if (w >= 0) return std::max(1, card(w).proto.range);
    return 1;
}

bool Room::inAttackRange(int from, int to) const { return distance(from, to) <= attackRange(from); }

int Room::equipId(int who, EquipSlot slot) const { return player(who).equipAt(slot); }

bool Room::hasEquipNamed(int who, EquipSlot slot, const char* name) const {
    int id = equipId(who, slot);
    return id >= 0 && card(id).name() == name;
}

bool Room::hasEquipNamed(int who, const char* name) const {
    for (int i = 0; i < 4; ++i) {
        int id = player(who).equip[i];
        if (id >= 0 && card(id).name() == name) return true;
    }
    return false;
}

// ============================================================ 牌区

Area Room::areaOf(int cardId) const {
    if (cardId < 0 || cardId >= static_cast<int>(loc_.size())) return Area::None;
    return loc_[cardId].area;
}

int Room::ownerOf(int cardId) const {
    if (cardId < 0 || cardId >= static_cast<int>(loc_.size())) return -1;
    return loc_[cardId].owner;
}

std::vector<int> Room::equipsOf(int owner) const {
    std::vector<int> out;
    for (int i = 0; i < 4; ++i) {
        int id = player(owner).equip[i];
        if (id >= 0) out.push_back(id);
    }
    return out;
}

std::vector<int> Room::allCardsOf(int owner) const {
    std::vector<int> out = player(owner).hand;
    for (int c : equipsOf(owner)) out.push_back(c);
    for (int c : player(owner).judgeZone) out.push_back(c);
    return out;
}

std::vector<int> Room::handCardsNamed(int owner, const char* name) const {
    std::vector<int> out;
    for (int c : player(owner).hand) {
        if (card(c).name() == name) out.push_back(c);
    }
    return out;
}

bool Room::hasCardNamed(int owner, const char* name) const {
    for (int c : allCardsOf(owner)) {
        if (card(c).name() == name) return true;
    }
    return false;
}

void Room::removeCard(int cardId) {
    if (cardId < 0 || cardId >= static_cast<int>(loc_.size())) return;
    CardLoc& l = loc_[cardId];
    switch (l.area) {
        case Area::Hand: eraseValue(players_[l.owner]->hand, cardId); break;
        case Area::Equip: {
            int slot = static_cast<int>(card(cardId).proto.slot);
            if (players_[l.owner]->equip[slot] == cardId) players_[l.owner]->equip[slot] = -1;
            break;
        }
        case Area::Judge: eraseValue(players_[l.owner]->judgeZone, cardId); break;
        case Area::DrawPile: eraseValue(drawPile_, cardId); break;
        case Area::DiscardPile: eraseValue(discardPile_, cardId); break;
        case Area::Revealed: eraseValue(revealedPile_, cardId); break;
        default: break;
    }
    l.area = Area::None;
    l.owner = -1;
}

void Room::addCard(int cardId, int owner, Area area) {
    if (cardId < 0 || cardId >= static_cast<int>(loc_.size())) return;
    CardLoc& l = loc_[cardId];
    l.area = area;
    l.owner = owner;
    switch (area) {
        case Area::Hand: players_[owner]->hand.push_back(cardId); break;
        case Area::Equip: {
            int slot = static_cast<int>(card(cardId).proto.slot);
            players_[owner]->equip[slot] = cardId;
            break;
        }
        case Area::Judge: players_[owner]->judgeZone.push_back(cardId); break;
        case Area::DrawPile: drawPile_.push_back(cardId); break;
        case Area::DiscardPile: discardPile_.push_back(cardId); break;
        case Area::Revealed: revealedPile_.push_back(cardId); break;
        default: break;
    }
}

// 所有区域里的牌数（不含结算中离场的牌）——用来做"牌数守恒"自检
int Room::totalCardsTracked() const {
    int n = static_cast<int>(drawPile_.size() + discardPile_.size() + revealedPile_.size());
    for (auto& p : players_) {
        n += p->handCount();
        for (int i = 0; i < 4; ++i) {
            if (p->equip[i] >= 0) ++n;
        }
        n += static_cast<int>(p->judgeZone.size());
    }
    return n;
}

void Room::moveCards(const std::vector<int>& ids, int toOwner, Area toArea,
                     const std::string& reason) {
    std::vector<int> moved;
    for (int c : ids) {
        if (c < 0 || areaOf(c) == Area::None) continue;
        removeCard(c);
        addCard(c, toOwner, toArea);
        moved.push_back(c);
    }
    if (moved.empty()) return;
    std::string label;
    for (size_t i = 0; i < moved.size(); ++i) {
        if (i) label += "、";
        label += cardLabel(moved[i]);
    }
    if (toOwner >= 0) {
        chat(str("  ", moved.size(), " 张牌移至 ", player(toOwner).name, " 的",
                 areaName(toArea), "：", label, "（", reason, "）"));
    } else {
        chat(str("  ", label, " 进入", areaName(toArea), "（", reason, "）"));
    }
}

void Room::discardCards(const std::vector<int>& ids, const std::string& reason) {
    std::vector<int> real;
    for (int c : ids) {
        if (c >= 0) real.push_back(c);
    }
    if (real.empty()) return;
    moveCards(real, -1, Area::DiscardPile, reason);
}

std::vector<int> Room::drawCards(int who, int n) {
    std::vector<int> out;
    for (int i = 0; i < n; ++i) {
        if (drawPile_.empty()) reshuffle();
        if (drawPile_.empty()) break;
        int c = drawPile_.back();
        drawPile_.pop_back();
        loc_[c].area = Area::Hand;
        loc_[c].owner = who;
        players_[who]->hand.push_back(c);
        out.push_back(c);
    }
    chat(str("  ", player(who).name, " 摸 ", static_cast<int>(out.size()), " 张牌"));
    return out;
}

// 亮出的牌放进"亮牌区"（而不是 Area::None）：
// 一是它们仍然属于场上（牌数守恒），二是 moveCards 会跳过 Area::None 的牌，
// 用 None 会导致"选中的牌送不进手里"这种静默 bug。
std::vector<int> Room::revealCards(int n) {
    std::vector<int> out;
    for (int i = 0; i < n; ++i) {
        if (drawPile_.empty()) reshuffle();
        if (drawPile_.empty()) break;
        int c = drawPile_.back();
        drawPile_.pop_back();
        addCard(c, -1, Area::Revealed);
        out.push_back(c);
    }
    return out;
}

void Room::equipCard(int who, int cardId) {
    EquipSlot slot = card(cardId).proto.slot;
    int old = equipId(who, slot);
    if (old >= 0) {
        removeCard(old);
        addCard(old, -1, Area::DiscardPile);
        chat(str("  ", player(who).name, " 替换下 ", cardLabel(old)));
    }
    removeCard(cardId);
    addCard(cardId, who, Area::Equip);
    chat(str("  ", player(who).name, " 装备 ", cardLabel(cardId)));
}

void Room::putJudgeCard(int owner, int cardId) {
    removeCard(cardId);
    cards_[cardId].playedBy = -1;
    addCard(cardId, owner, Area::Judge);
    chat(str("  ", cardLabel(cardId), " 置于 ", player(owner).name, " 的判定区"));
}

// ============================================================ 距离 / 牌堆工具

void Room::shuffle(std::vector<int>& pile) { std::shuffle(pile.begin(), pile.end(), rng_); }

void Room::reshuffle() {
    if (discardPile_.empty()) return;
    chat(str("  牌堆已空，洗牌：", static_cast<int>(discardPile_.size()), " 张弃牌重新入堆"));
    for (int c : discardPile_) {
        loc_[c].area = Area::DrawPile;
        loc_[c].owner = -1;
        drawPile_.push_back(c);
    }
    discardPile_.clear();
    shuffle(drawPile_);
}

void Room::clearPlayerCards(int who) {
    std::vector<int> cards = allCardsOf(who);
    if (!cards.empty()) moveCards(cards, -1, Area::DiscardPile, "阵亡弃置所有牌");
}

// ============================================================ 事件系统

void Room::registerTrigger(const std::string& name, Event e, int owner, int priority,
                           std::function<bool(Room&, TriggerParams&)> fn) {
    auto t = std::make_shared<Trigger>();
    t->name = name;
    t->event = e;
    t->owner = owner;
    t->priority = priority;
    t->fn = std::move(fn);
    triggers_.push_back(t);
}

void Room::clearTriggersNamed(const std::string& name) {
    triggers_.erase(std::remove_if(triggers_.begin(), triggers_.end(),
                                   [&](const std::shared_ptr<Trigger>& t) {
                                       return t->name == name;
                                   }),
                    triggers_.end());
}

bool Room::trigger(Event e, TriggerParams& p) {
    std::vector<std::shared_ptr<Trigger>> list = triggers_;
    std::stable_sort(list.begin(), list.end(),
                     [](const std::shared_ptr<Trigger>& a, const std::shared_ptr<Trigger>& b) {
                         return a->priority < b->priority;
                     });
    for (auto& t : list) {
        if (t->event != e) continue;
        if (t->owner >= 0 && !player(t->owner).alive) continue;
        if (!t->fn(*this, p)) return false;
    }
    return true;
}

// ============================================================ 询问

AskResult Room::askAgent(const Ask& a) {
    if (a.who < 0 || a.who >= static_cast<int>(players_.size())) return AskResult{};
    Agent* agent = players_[a.who]->agent.get();
    if (!agent) return AskResult{};
    return agent->ask(*this, a);
}

int Room::askForResponse(int who, const char* cardName, const std::string& prompt, int source) {
    Player& p = player(who);
    if (!p.alive) return -1;

    std::vector<int> matches = handCardsNamed(who, cardName);

    // 装备技能可以提供替代响应（八卦阵）
    TriggerParams tp;
    tp.event = Event::AskForCard;
    tp.who = who;
    tp.source = source;
    tp.askName = cardName;
    tp.reason = prompt;
    tp.askCandidates = matches;
    trigger(Event::AskForCard, tp);
    if (tp.provided) {
        if (tp.providedCard >= 0) discardCards({tp.providedCard}, "替代响应");
        return kVirtualCard;
    }

    if (matches.empty()) return -1;

    Ask a;
    a.kind = AskKind::Card;
    a.tag = cardName;
    a.who = who;
    a.cards = matches;
    a.subject = source;
    a.prompt = prompt;
    a.minCards = 1;
    a.maxCards = 1;
    a.allowCancel = true;
    AskResult r = askAgent(a);
    if (r.cards.size() != 1) return -1;
    int c = r.cards[0];
    if (!contains(matches, c)) return -1;
    removeCard(c);
    addCard(c, -1, Area::DiscardPile);
    chat(str("  ", p.name, " 打出 ", cardLabel(c)));
    return c;
}

std::vector<int> Room::askTargets(int user, const std::string& cardDisplay,
                                  const std::vector<int>& candidates, int minT, int maxT) {
    if (candidates.empty()) return {};
    if (minT == maxT && static_cast<int>(candidates.size()) == minT) return candidates;

    Ask a;
    a.kind = AskKind::Target;
    a.tag = "Target";
    a.who = user;
    a.targetCandidates = candidates;
    a.minTargets = minT;
    a.maxTargets = maxT;
    a.prompt = str("为 ", cardDisplay, " 选择 ", (minT == maxT ? str(minT) : str(minT, "-", maxT)),
                   " 个目标");
    AskResult r = askAgent(a);

    std::vector<int> out;
    for (int t : r.targets) {
        if (contains(candidates, t) && !contains(out, t)) out.push_back(t);
    }
    if (static_cast<int>(out.size()) < minT) return {};
    if (static_cast<int>(out.size()) > maxT) out.resize(maxT);
    return out;
}

// ============================================================ 使用牌

std::string Room::cardLabel(int cardId) const {
    if (cardId < 0) return "(无)";
    return sgs::cardLabel(cards_[cardId].proto, cards_[cardId].virtualCard);
}

std::string Room::useLabel(const CardUseStruct& use) const {
    return sgs::cardLabel(use.proto, use.virtualCard);
}

void Room::announceUse(const CardUseStruct& use) {
    std::string t;
    for (size_t i = 0; i < use.targets.size(); ++i) {
        if (i) t += "、";
        t += playerName(use.targets[i]);
    }
    if (t.empty()) t = "（无目标）";
    std::string extra;
    if (use.virtualCard && !use.cards.empty()) {
        extra = "（";
        for (size_t i = 0; i < use.cards.size(); ++i) {
            if (i) extra += "+";
            extra += cardLabel(use.cards[i]);
        }
        extra += "）";
    }
    chat(str(playerName(use.user), " 使用 ", useLabel(use), extra, " → ", t));
}

bool Room::useCard(CardUseStruct& use) {
    if (use.user < 0 || !player(use.user).alive) return false;

    // 1) 支付：把实体牌从原区域取出（暂存"离场"状态，结算完再进弃牌堆）
    for (int c : use.cards) {
        if (c >= 0 && areaOf(c) != Area::None) removeCard(c);
    }

    // 2) CardUsed 事件：可以修改目标（方天画戟）、记录使用（连弩计数等）
    TriggerParams tp;
    tp.event = Event::CardUsed;
    tp.who = use.user;
    tp.source = use.user;
    tp.use = &use;
    tp.cardId = use.cardId;
    trigger(Event::CardUsed, tp);

    announceUse(use);

    // 【五谷丰登】在使用时就把牌亮出来（正式规则里这是公开信息）：
    // 否则"是否用【无懈可击】"的询问发生在亮牌之前，玩家根本没法判断。
    if (use.proto.name == CardName::Harvest && use.sharedCards.empty()) {
        use.sharedCards = revealCards(static_cast<int>(use.targets.size()));
        std::string label;
        for (size_t i = 0; i < use.sharedCards.size(); ++i) {
            if (i) label += "、";
            label += cardLabel(use.sharedCards[i]);
        }
        chat(str("  【五谷丰登】亮出：", label));
    }

    // 3) 装备牌：直接进装备区
    if (use.proto.type == CardType::Equip) {
        int cid = use.cardId >= 0 ? use.cardId : (use.cards.empty() ? -1 : use.cards[0]);
        if (cid >= 0) equipCard(use.user, cid);
        return true;
    }

    // 4) 延时锦囊：置于目标判定区
    if (use.proto.type == CardType::DelayedTrick) {
        for (int t : use.targets) {
            if (!player(t).alive) continue;
            putJudgeCard(t, use.cardId);
            cards_[use.cardId].playedBy = use.user;
        }
        return true;
    }

    // 5) 逐目标结算：先问无懈，再触发 CardEffect，最后执行效果
    auto targets = use.targets;
    for (int t : targets) {
        if (!player(t).alive) continue;
        if (askNullification(use, t)) {
            chat(str("  ", useLabel(use), " 对 ", playerName(t), " 的效果被【无懈可击】抵消"));
            continue;
        }
        TriggerParams ce;
        ce.event = Event::CardEffect;
        ce.who = t;
        ce.source = use.user;
        ce.target = t;
        ce.cardId = use.cardId;
        ce.use = &use;
        trigger(Event::CardEffect, ce);
        if (ce.effectCancelled) {
            chat(str("  ", useLabel(use), " 对 ", playerName(t), " 的效果被取消"));
            continue;
        }
        applyCardEffect(*this, use, t);
        if (gameOver()) break;
    }

    // 6) 收尾：五谷丰登剩余的亮牌、以及使用掉的牌进弃牌堆
    if (!use.sharedCards.empty()) {
        discardCards(use.sharedCards, "五谷丰登剩余亮牌");
        use.sharedCards.clear();
    }
    for (int c : use.cards) {
        if (c >= 0 && areaOf(c) == Area::None) addCard(c, -1, Area::DiscardPile);
    }
    if (use.cardId >= 0 && areaOf(use.cardId) == Area::None) {
        addCard(use.cardId, -1, Area::DiscardPile);
    }
    return true;
}

// 无懈可击：奇偶翻转的简化链式结算（A 无懈 → B 无懈抵销 → 效果恢复 …）
bool Room::askNullification(const CardUseStruct& use, int target) {
    if (use.proto.type != CardType::Trick && use.proto.type != CardType::DelayedTrick) return false;
    if (use.proto.name == CardName::Nullification) return false;  // 无懈本身由上层奇偶链处理

    // 把"这张锦囊亮出的牌"一并告诉玩家，否则没法决定要不要无懈
    std::string revealInfo;
    if (!use.sharedCards.empty()) {
        revealInfo = "（亮出：";
        for (size_t i = 0; i < use.sharedCards.size(); ++i) {
            if (i) revealInfo += "、";
            revealInfo += cardLabel(use.sharedCards[i]);
        }
        revealInfo += "）";
    }

    bool cancelled = false;
    int starter = use.user;
    int guard = 0;
    while (guard++ < 64) {
        int nuser = -1;
        int ncard = -1;
        for (int i = 1; i <= playerCount(); ++i) {
            int who = (starter + i) % playerCount();
            if (!player(who).alive) continue;
            std::vector<int> cards = handCardsNamed(who, CardName::Nullification);
            if (cards.empty()) continue;
            Ask a;
            a.kind = AskKind::Card;
            a.tag = CardName::Nullification;
            a.who = who;
            a.subject = target;
            a.source = use.user;
            a.cards = cards;
            a.context = cancelled ? "revert" : "cancel";
            if (cancelled) {
                a.prompt = str("是否对【无懈可击】使用【无懈可击】？（当前效果已被抵消，出牌将使其重新生效）",
                               revealInfo);
            } else {
                a.prompt = str("是否对 ", playerName(use.user), " 的 ", useLabel(use), "（目标 ",
                               playerName(target), "）使用【无懈可击】？", revealInfo);
            }
            AskResult r = askAgent(a);
            if (r.cards.size() == 1 && contains(cards, r.cards[0])) {
                nuser = who;
                ncard = r.cards[0];
                break;
            }
        }
        if (nuser < 0) break;
        chat(str("  ", playerName(nuser), " 使用 【无懈可击】(对 ", useLabel(use), " 目标 ",
                 playerName(target), ")", cancelled ? "，使其重新生效" : ""));
        removeCard(ncard);
        addCard(ncard, -1, Area::DiscardPile);
        cancelled = !cancelled;
        starter = nuser;
    }
    return cancelled;
}

// ============================================================ 判定

int Room::judge(int who, const std::string& reason, JudgeStruct* out) {
    if (drawPile_.empty()) reshuffle();
    if (drawPile_.empty()) return -1;
    int cid = drawPile_.back();
    drawPile_.pop_back();
    loc_[cid].area = Area::None;
    loc_[cid].owner = -1;

    JudgeStruct js;
    js.who = who;
    js.cardId = cid;
    js.reason = reason;

    TriggerParams tp;
    tp.event = Event::Judge;
    tp.who = who;
    tp.judge = &js;
    trigger(Event::Judge, tp);

    chat(str("  【判定】", playerName(who), "：", cardLabel(js.cardId), "（", reason, "）"));

    // 判定牌进弃牌堆（改判技能可在此前替换 js.cardId，本引擎无武将）
    addCard(js.cardId, -1, Area::DiscardPile);
    if (out) *out = js;
    return js.cardId;
}

void Room::processJudgeZone(int who) {
    std::vector<int> list = player(who).judgeZone;
    // 后放先判
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        int cid = *it;
        if (areaOf(cid) != Area::Judge) continue;
        const Card& c = card(cid);
        CardUseStruct use;
        use.user = c.playedBy >= 0 ? c.playedBy : who;
        use.cardId = cid;
        use.proto = c.proto;
        use.targets = {who};

        if (askNullification(use, who)) {
            chat(str("  ", cardLabel(cid), " 被【无懈可击】抵消，直接进入弃牌堆"));
            moveCards({cid}, -1, Area::DiscardPile, "无懈抵消延时锦囊");
            continue;
        }

        if (c.name() == CardName::Indulgence) {
            JudgeStruct js;
            judge(who, "乐不思蜀", &js);
            bool heart = card(js.cardId).proto.suit == Suit::Heart;
            if (heart) {
                chat(str("  判定为红桃，", playerName(who), " 不受【乐不思蜀】影响"));
            } else {
                player(who).skipPlayPhase = true;
                chat(str("  判定非红桃，", playerName(who), " 跳过本回合出牌阶段"));
            }
            moveCards({cid}, -1, Area::DiscardPile, "乐不思蜀结算完毕");
        } else if (c.name() == CardName::Lightning) {
            JudgeStruct js;
            judge(who, "闪电", &js);
            const CardProto& jp = card(js.cardId).proto;
            if (jp.suit == Suit::Spade && jp.rank >= 2 && jp.rank <= 9) {
                chat(str("  判定为 ", suitName(jp.suit), rankLabel(jp.rank), "，闪电击中 ",
                         playerName(who), "！"));
                moveCards({cid}, -1, Area::DiscardPile, "闪电命中");
                DamageStruct d;
                d.from = -1;
                d.to = who;
                d.cardId = cid;
                d.amount = 3;
                d.nature = DamageNature::Thunder;
                d.fromCard = true;
                damage(d);
            } else {
                int nxt = other(who);
                chat(str("  判定未命中，闪电移动到 ", playerName(nxt), " 的判定区"));
                moveCards({cid}, nxt, Area::Judge, "闪电转移");
            }
        }
        if (gameOver()) return;
    }
}

// ============================================================ 伤害 / 体力

bool Room::damage(DamageStruct d) {
    if (d.to < 0 || !player(d.to).alive) return false;
    TriggerParams pre;
    pre.event = Event::Damage;
    pre.who = d.to;
    pre.source = d.from;
    pre.damage = &d;
    pre.cardId = d.cardId;
    trigger(Event::Damage, pre);
    if (d.amount <= 0) return false;

    Player& to = player(d.to);
    to.hp -= d.amount;
    chat(str("  ", playerName(d.from), " 对 ", to.name, " 造成 ", d.amount, " 点",
             natureName(d.nature), "伤害（体力 ", to.hp, "/", to.maxHp, "）"));

    TriggerParams post;
    post.event = Event::Damaged;
    post.who = d.to;
    post.source = d.from;
    post.damage = &d;
    post.cardId = d.cardId;
    trigger(Event::Damaged, post);

    if (to.hp <= 0) enterDying(d.to, d.from);
    return true;
}

void Room::recover(int who, int amount) {
    Player& p = player(who);
    if (!p.alive || amount <= 0) return;
    int before = p.hp;
    p.hp = std::min(p.maxHp, p.hp + amount);
    if (p.hp != before) {
        chat(str("  ", p.name, " 回复 ", p.hp - before, " 点体力（体力 ", p.hp, "/", p.maxHp, "）"));
        TriggerParams tp;
        tp.event = Event::Recover;
        tp.who = who;
        tp.number = p.hp - before;
        trigger(Event::Recover, tp);
    }
}

void Room::loseHp(int who, int amount) {
    Player& p = player(who);
    if (!p.alive || amount <= 0) return;
    p.hp -= amount;
    chat(str("  ", p.name, " 失去 ", amount, " 点体力（体力 ", p.hp, "/", p.maxHp, "）"));
    if (p.hp <= 0) enterDying(who, -1);
}

void Room::enterDying(int who, int killer) {
    Player& p = player(who);
    if (!p.alive) return;
    chat(str("  ", p.name, " 进入濒死状态！"));

    TriggerParams tp;
    tp.event = Event::Dying;
    tp.who = who;
    tp.source = killer;
    trigger(Event::Dying, tp);

    int start = current_ >= 0 ? current_ : who;
    int guard = 0;
    while (p.alive && p.hp <= 0 && guard++ < 200) {
        bool used = false;
        for (int i = 0; i < playerCount() && !used; ++i) {
            int helper = (start + i) % playerCount();
            if (!player(helper).alive) continue;
            std::vector<int> peaches = handCardsNamed(helper, CardName::Peach);
            if (peaches.empty()) continue;
            Ask a;
            a.kind = AskKind::Card;
            a.tag = CardName::Peach;
            a.who = helper;
            a.subject = who;
            a.cards = peaches;
            a.prompt = str("是否使用【桃】救助 ", p.name, "（体力 ", p.hp, "/", p.maxHp, "）？");
            AskResult r = askAgent(a);
            if (r.cards.size() == 1 && contains(peaches, r.cards[0])) {
                removeCard(r.cards[0]);
                addCard(r.cards[0], -1, Area::DiscardPile);
                chat(str("  ", playerName(helper), " 使用 ", cardLabel(r.cards[0]), " 救助 ", p.name));
                recover(who, 1);
                used = true;
            }
        }
        if (!used) break;
    }

    if (p.hp <= 0) killPlayer(who, killer);
}

void Room::killPlayer(int who, int killer) {
    Player& p = player(who);
    if (!p.alive) return;
    p.alive = false;
    chat(str("  ", p.name, " 阵亡！"));
    clearPlayerCards(who);

    TriggerParams tp;
    tp.event = Event::Die;
    tp.who = who;
    tp.source = killer;
    trigger(Event::Die, tp);

    checkGameOver();
}

void Room::checkGameOver() {
    if (gameOver()) return;
    auto alive = alivePlayers();
    if (alive.size() == 1) {
        winner_ = alive[0];
    } else if (alive.empty()) {
        winner_ = -2;  // 同归于尽
    }
}

// ============================================================ 输出

void Room::chat(const std::string& s) {
    if (!silent_) std::cout << s << std::endl;
    log_.push_back(s);
    if (logSink_) logSink_(s);
}

// ============================================================ 回合流程

void Room::runTurn(int who) {
    current_ = who;
    ++turn_;
    Player& p = player(who);
    p.slashUsed = 0;
    p.skipPlayPhase = false;

    chat("");
    chat(str("===== 第 ", turn_, " 回合：", p.name, "（体力 ", p.hp, "/", p.maxHp, "，手牌 ",
             p.handCount(), "）====="));

    TriggerParams ts;
    ts.event = Event::TurnStart;
    ts.who = who;
    trigger(Event::TurnStart, ts);

    const Phase phases[] = {Phase::RoundStart, Phase::Judge, Phase::Draw, Phase::Play,
                            Phase::Discard, Phase::RoundEnd};
    for (Phase ph : phases) {
        if (gameOver() || !player(who).alive) break;

        TriggerParams ps;
        ps.event = Event::PhaseStart;
        ps.who = who;
        ps.number = static_cast<int>(ph);
        trigger(Event::PhaseStart, ps);

        switch (ph) {
            case Phase::Judge: processJudgeZone(who); break;
            case Phase::Draw: runDrawPhase(who); break;
            case Phase::Play: runPlayPhase(who); break;
            case Phase::Discard: runDiscardPhase(who); break;
            default: break;
        }

        TriggerParams pe;
        pe.event = Event::PhaseEnd;
        pe.who = who;
        pe.number = static_cast<int>(ph);
        trigger(Event::PhaseEnd, pe);
    }

    TriggerParams te;
    te.event = Event::TurnEnd;
    te.who = who;
    trigger(Event::TurnEnd, te);

    player(who).skipPlayPhase = false;
    current_ = -1;
}

void Room::runDrawPhase(int who) {
    chat("  —— 摸牌阶段 ——");
    drawCards(who, 2);
}

void Room::runPlayPhase(int who) {
    Player& p = player(who);
    chat("  —— 出牌阶段 ——");
    if (p.skipPlayPhase) {
        chat(str("  ", p.name, " 跳过出牌阶段"));
        return;
    }

    int guard = 0;
    while (!gameOver() && guard++ < 200) {
        std::vector<PlayOption> options = buildPlayOptions(*this, who);
        if (options.empty()) break;

        Ask a;
        a.kind = AskKind::Option;
        a.tag = "Play";
        a.who = who;
        a.prompt = "出牌阶段：请选择要使用的牌（放弃则结束出牌阶段）";
        for (auto& o : options) {
            a.options.push_back(o.text);
            a.optionCards.push_back(o.virtualCard ? -1 : o.cardId);
        }
        AskResult r = askAgent(a);
        if (r.option < 0 || r.option >= static_cast<int>(options.size())) break;

        PlayOption opt = options[r.option];

        // 目标
        std::vector<int> targets = opt.presetTargets;
        if (opt.needAskTargets) {
            targets = askTargets(who, opt.proto.display, opt.targetCandidates, opt.minTargets,
                                 opt.maxTargets);
            if (targets.empty()) {
                chat(str("  ", p.name, " 取消了 ", opt.proto.display, " 的使用"));
                continue;
            }
        }

        // 丈八蛇矛：两张手牌当【杀】
        std::vector<int> pay = opt.cards;
        if (opt.virtualCard) {
            Ask m;
            m.kind = AskKind::MultiCard;
            m.tag = "ViewAs";
            m.who = who;
            m.cards = p.hand;
            m.minCards = 2;
            m.maxCards = 2;
            m.prompt = "【丈八蛇矛】：请选择两张手牌作为【杀】的素材";
            AskResult mr = askAgent(m);
            if (mr.cards.size() != 2) continue;
            pay = mr.cards;
        }

        CardUseStruct use;
        use.user = who;
        use.cardId = opt.cardId;
        use.cards = pay;
        use.proto = opt.proto;
        use.virtualCard = opt.virtualCard;
        use.targets = targets;

        bool ok = useCard(use);
        if (ok && opt.proto.name == CardName::Slash) ++p.slashUsed;
        if (gameOver()) return;
    }
}

void Room::runDiscardPhase(int who) {
    chat("  —— 弃牌阶段 ——");
    Player& p = player(who);
    int limit = std::max(0, p.hp);
    int need = p.handCount() - limit;
    if (need <= 0) {
        chat(str("  手牌 ", p.handCount(), " ≤ 体力 ", limit, "，无需弃牌"));
        return;
    }

    Ask a;
    a.kind = AskKind::MultiCard;
    a.tag = "Discard";
    a.who = who;
    a.cards = p.hand;
    a.minCards = need;
    a.maxCards = need;
    a.prompt = str("弃牌阶段：请弃置 ", need, " 张手牌（手牌上限 = 体力 ", limit, "）");
    AskResult r = askAgent(a);

    std::vector<int> picked;
    for (int c : r.cards) {
        if (contains(p.hand, c) && !contains(picked, c)) picked.push_back(c);
    }
    while (static_cast<int>(picked.size()) < need && !p.hand.empty()) {
        int c = p.hand[0];
        if (!contains(picked, c)) picked.push_back(c);
        else break;
    }
    if (static_cast<int>(picked.size()) > need) picked.resize(need);
    discardCards(picked, "弃牌阶段");
}

int Room::run() {
    TriggerParams tp;
    tp.event = Event::GameStart;
    trigger(Event::GameStart, tp);

    chat(str("=== 1v1 对战开始：", playerName(0), " vs ", playerName(1), " ==="));
    for (int i = 0; i < playerCount(); ++i) {
        chat(str("  起始手牌：", playerName(i)));
        drawCards(i, kInitHand);
    }

    int who = 0;
    while (!gameOver()) {
        if (!player(who).alive) {
            who = other(who);
            if (!player(who).alive) break;
        }
        runTurn(who);
        if (gameOver()) break;
        if (turn_ >= maxTurns_) {
            chat("  达到回合上限，判定为平局");
            winner_ = -2;
            break;
        }
        who = other(who);
    }

    if (winner_ >= 0) {
        chat(str("=== 游戏结束：", playerName(winner_), " 获胜（共 ", turn_, " 回合）==="));
    } else {
        chat(str("=== 游戏结束：平局（共 ", turn_, " 回合）==="));
    }

    // 收尾自检：这时候不该有任何牌还停在"结算中"，所有牌都必须落在某个区域里。
    // 这条告警必须"静默模式下也能看到"（--quiet 跑批/CI 要靠它抓丢牌），
    // 所以除了 chat（会写日志、外送 sink）之外再单独写一次 stderr。
    int tracked = totalCardsTracked();
    if (tracked != deckSize()) {
        std::string warn = str("  ⚠ 牌数守恒检查失败：场上 ", tracked, " / 整副 ", deckSize(),
                               "（有 ", deckSize() - tracked, " 张牌被效果吃掉了）");
        chat(warn);
        log_.push_back(warn);
        if (logSink_) logSink_(warn);
        std::cerr << warn << std::endl;
    }
    return winner_;
}

}  // namespace sgs
