// ============================================================================
//  WebAssembly 胶水层
//  用 Emscripten 把引擎（src/ 下的 C++ 内核）编进浏览器，把"玩家决策"这一步
//  交给网页：C++ 侧用 emscripten_sleep（Asyncify）挂起，等页面点完再继续。
//
//  编译见 web/build_web.bat
// ============================================================================
#include <emscripten.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "AiAgent.h"
#include "CardDatabase.h"
#include "CardEffects.h"
#include "Room.h"
#include "Util.h"

using namespace sgs;

// ---------------------------------------------------------------- JS 桥
// 注意：这些都是"从 wasm 调进 JS"的同步函数。反过来（页面点按钮 → 通知 C++）
// 只写 JS 全局变量，绝不能在 C++ 处于 sleep 时再调进 wasm，否则 Asyncify 会报错。
EM_JS(void, js_log, (const char* s), {
    if (globalThis.sgsOnLog) globalThis.sgsOnLog(UTF8ToString(s));
});
EM_JS(void, js_state, (const char* s), {
    if (globalThis.sgsOnState) globalThis.sgsOnState(UTF8ToString(s));
});
EM_JS(void, js_ask, (const char* s), {
    if (globalThis.sgsOnAsk) globalThis.sgsOnAsk(UTF8ToString(s));
});
EM_JS(void, js_event, (const char* s), {
    if (globalThis.sgsOnEvent) globalThis.sgsOnEvent(UTF8ToString(s));
});
EM_JS(void, js_ask_closed, (int requestId), {
    if (globalThis.sgsOnAskClosed) globalThis.sgsOnAskClosed(requestId);
});
EM_JS(void, js_done, (), {
    if (globalThis.sgsOnDone) globalThis.sgsOnDone();
});
EM_JS(int, js_answer_ready, (int requestId), {
    return (globalThis.sgsAnswer && globalThis.sgsAnswer.ready &&
            globalThis.sgsAnswer.requestId === requestId) ? 1 : 0;
});
EM_JS(int, js_answer_option, (), {
    return globalThis.sgsAnswer ? (globalThis.sgsAnswer.option | 0) : -1;
});
// 注意：EM_JS 的 JS 体会被 clang 当 C++ 词法扫一遍，别在里面写空字符常量 ''，
//       用 "" 代替，否则会出 "-Winvalid-pp-token: empty character constant" 警告。
EM_JS(void, js_answer_cards, (char* buf, int len), {
    var s = (globalThis.sgsAnswer && globalThis.sgsAnswer.cards)
        ? globalThis.sgsAnswer.cards.join(",") : "";
    stringToUTF8(s, buf, len);
});
EM_JS(void, js_answer_targets, (char* buf, int len), {
    var s = (globalThis.sgsAnswer && globalThis.sgsAnswer.targets)
        ? globalThis.sgsAnswer.targets.join(",") : "";
    stringToUTF8(s, buf, len);
});
EM_JS(void, js_answer_clear, (), {
    globalThis.sgsAnswer = { ready: false, option: -1, cards: [], targets: [] };
});
EM_JS(int, js_speed_ms, (), {
    return globalThis.sgsSpeedMs | 0;
});
EM_JS(int, js_auto_play, (), {
    return globalThis.sgsAutoPlay ? 1 : 0;
});
// 【八卦阵】发动方式：页面菜单里随时可改，写在这个全局变量里（0/1/2）
EM_JS(int, js_bagua_mode, (), {
    return globalThis.sgsBaguaMode | 0;
});

namespace {

std::unique_ptr<Room> g_room;
int g_humanSeat = -1;
int g_requestId = 0;  // 跨局递增，旧询问的迟到点击不会进入新局
Phase g_phase = Phase::None;
int g_useSequence = 0;
std::vector<int> g_useGroups;

// ---------------------------------------------------------------- JSON 工具
std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n' || c == '\r' || c == '\t') o += ' ';
        else o += c;
    }
    return o;
}

// 装备槽的显示名：坐骑不写 ±1，因为牌名里已经带了（【紫骍 -1马】），避免重复
const char* slotLabel(int i) {
    switch (i) {
        case 0: return "武器";
        case 1: return "防具";
        default: return "坐骑";
    }
}

const char* kindName(AskKind k) {
    switch (k) {
        case AskKind::Card: return "card";
        case AskKind::MultiCard: return "multi";
        case AskKind::Option: return "option";
        default: return "target";
    }
}

std::string protoJson(const CardProto& p, int id) {
    return str("{\"id\":", id, ",\"name\":\"", jsonEscape(p.name),
               "\",\"display\":\"", jsonEscape(p.display), "\",\"label\":\"",
               jsonEscape(sgs::cardLabel(p)), "\",\"suit\":\"", suitSymbol(p.suit),
               "\",\"rank\":\"", rankLabel(p.rank), "\",\"type\":\"", typeName(p.type),
               "\",\"slotIndex\":", static_cast<int>(p.slot), ",\"range\":", p.range, "}");
}

std::string cardJson(Room& room, int id, bool hidden = false) {
    if (hidden) return str("{\"id\":", id, ",\"hidden\":true,\"label\":\"未知手牌\",\"area\":\"手牌\"}");
    std::string j = protoJson(room.card(id).proto, id);
    j.pop_back();
    return j + str(",\"area\":\"", areaName(room.areaOf(id)), "\"}");
}

std::string cardArray(Room& room, const std::vector<int>& ids) {
    std::string j = "[";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) j += ",";
        j += cardJson(room, ids[i]);
    }
    j += "]";
    return j;
}

std::string playerJson(Room& room, int id) {
    Player& p = room.player(id);
    std::string j = "{";
    j += str("\"id\":", p.id, ",\"name\":\"", jsonEscape(p.name), "\",\"hp\":", p.hp,
             ",\"maxHp\":", p.maxHp, ",\"alive\":", p.alive ? "true" : "false",
             ",\"handCount\":", p.handCount(), ",\"range\":", room.attackRange(p.id),
             ",\"distance\":", room.distance(id, room.other(id)),
             ",\"slashUsed\":", p.slashUsed, ",");
    // 人机模式仅发送自己的手牌；观战公开双方手牌。
    j += "\"hand\":" + (g_humanSeat < 0 || id == g_humanSeat ? cardArray(room, p.hand) : "[]") + ",";

    std::string eq = "[";
    for (int i = 0; i < 4; ++i) {
        int c = p.equip[i];
        if (c < 0) continue;
        if (eq.size() > 1) eq += ",";
        std::string item = cardJson(room, c);
        item.pop_back();
        eq += item + str(",\"slot\":\"", slotLabel(i), "\"}");
    }
    eq += "]";
    j += "\"equip\":" + eq + ",";
    j += "\"judge\":" + cardArray(room, p.judgeZone);
    j += "}";
    return j;
}

std::string stateJson(Room& room) {
    std::string j = "{";
    j += str("\"protocol\":2,\"phase\":\"", phaseName(g_phase), "\",\"turn\":", room.turnCount(), ",\"current\":", room.currentPlayer(),
             ",\"over\":", room.gameOver() ? "true" : "false",
             ",\"winner\":", room.winner(), ",\"deck\":", room.drawPileSize(),
             ",\"discard\":", room.discardPileSize(), ",");
    j += "\"discardTop\":" + (room.discardTop() >= 0 ? cardJson(room, room.discardTop()) : "null") + ",";
    j += "\"revealed\":" + cardArray(room, room.revealedCards()) + ",";
    j += "\"players\":[" + playerJson(room, 0) + "," + playerJson(room, 1) + "]";
    j += "}";
    return j;
}

void pushState() {
    if (g_room) js_state(stateJson(*g_room).c_str());
}

std::string askJson(Room& room, const Ask& a, int requestId) {
    std::string j = "{";
    j += str("\"requestId\":", requestId, ",\"allowCancel\":", a.allowCancel ? "true" : "false", ",");
    j += str("\"kind\":\"", kindName(a.kind), "\",");
    j += str("\"tag\":\"", jsonEscape(a.tag), "\",");
    j += str("\"prompt\":\"", jsonEscape(a.prompt), "\",");
    j += str("\"who\":", a.who, ",\"subject\":", a.subject, ",\"source\":", a.source, ",");
    j += str("\"context\":\"", jsonEscape(a.context), "\",");
    j += str("\"minCards\":", a.minCards, ",\"maxCards\":", a.maxCards, ",");
    j += str("\"minTargets\":", a.minTargets, ",\"maxTargets\":", a.maxTargets, ",");

    j += "\"cards\":[";
    for (size_t i = 0; i < a.cards.size(); ++i) {
        int c = a.cards[i];
        bool hide = a.hidden && room.areaOf(c) == Area::Hand;
        if (i) j += ",";
        j += cardJson(room, c, hide);
    }
    j += "],\"options\":[";
    for (size_t i = 0; i < a.options.size(); ++i) {
        if (i) j += ",";
        j += str("\"", jsonEscape(a.options[i]), "\"");
    }
    j += "],\"optionCards\":[";
    for (size_t i = 0; i < a.optionCards.size(); ++i) {
        if (i) j += ",";
        j += str(a.optionCards[i]);
    }
    j += "],\"optionDetails\":[";
    auto play = a.tag == "Play" ? buildPlayOptions(room, a.who) : std::vector<PlayOption>{};
    for (size_t i = 0; i < a.options.size(); ++i) {
        if (i) j += ",";
        int c = i < a.optionCards.size() ? a.optionCards[i] : -1;
        j += "{\"card\":";
        if (c >= 0) j += cardJson(room, c);
        else if (i < play.size()) j += protoJson(play[i].proto, -1);
        else j += "null";
        j += ",\"targets\":[";
        if (i < play.size()) {
            const auto& ts = play[i].needAskTargets ? play[i].targetCandidates : play[i].presetTargets;
            for (size_t n = 0; n < ts.size(); ++n) {
                if (n) j += ",";
                j += str(ts[n]);
            }
        }
        j += "]}";
    }
    j += "],\"targets\":[";
    for (size_t i = 0; i < a.targetCandidates.size(); ++i) {
        int t = a.targetCandidates[i];
        if (i) j += ",";
        j += str("{\"id\":", t, ",\"name\":\"", jsonEscape(room.playerName(t)), "\",\"hp\":",
                 room.player(t).hp, ",\"maxHp\":", room.player(t).maxHp, ",\"alive\":",
                 room.player(t).alive ? "true" : "false", "}");
    }
    j += "]}";
    return j;
}

// ---------------------------------------------------------------- Web Agent
// human = true：把询问推给页面，然后挂起等待；
// human = false：直接让启发式 AI 决策（观战模式），按页面的速度设置做一点延迟。
class WebAgent : public Agent {
public:
    WebAgent(bool human, std::string label) : human_(human), label_(std::move(label)) {}

    std::string name() const override { return label_; }

    AskResult ask(Room& room, const Ask& a) override {
        pushState();

        if (!human_ || js_auto_play() == 1) {
            int ms = js_speed_ms();
            if (ms > 0) emscripten_sleep(static_cast<unsigned>(ms));
            return ai_.ask(room, a);
        }

        // 顺序很重要：先清空答案槽，再发布询问。
        // 否则"发布 → 同步作答（自动化脚本/同帧点击）→ 被 clear 吃掉"会死等。
        const int requestId = ++g_requestId;
        js_answer_clear();
        js_ask(askJson(room, a, requestId).c_str());

        // 等页面把答案写进 globalThis.sgsAnswer；sleep 让出事件循环，页面才能刷新
        while (js_answer_ready(requestId) == 0) {
            // 托管当前询问也由 AI 正常作答，不能把空答案当成放弃。
            if (js_auto_play() == 1) {
                js_answer_clear();
                js_ask_closed(requestId);
                return ai_.ask(room, a);
            }
            emscripten_sleep(30);
        }

        char buf[2048];
        AskResult r;
        r.option = js_answer_option();
        js_answer_cards(buf, static_cast<int>(sizeof(buf)));
        r.cards = parseIntList(buf);
        js_answer_targets(buf, static_cast<int>(sizeof(buf)));
        r.targets = parseIntList(buf);
        js_answer_clear();

        // 合法性校验：只保留候选里的东西
        js_ask_closed(requestId);
        std::vector<int> cards;
        for (int c : r.cards) {
            if (contains(a.cards, c) && !contains(cards, c)) cards.push_back(c);
        }
        if (static_cast<int>(cards.size()) > a.maxCards && a.maxCards > 0) cards.resize(a.maxCards);
        r.cards = cards;

        std::vector<int> targets;
        for (int t : r.targets) {
            if (contains(a.targetCandidates, t) && !contains(targets, t)) targets.push_back(t);
        }
        if (static_cast<int>(targets.size()) > a.maxTargets && a.maxTargets > 0) {
            targets.resize(a.maxTargets);
        }
        r.targets = targets;

        if (r.option < 0 || r.option >= static_cast<int>(a.options.size())) r.option = -1;

        if (a.kind == AskKind::Card && cards.empty()) r.option = -1;
        if (a.kind == AskKind::Target && static_cast<int>(targets.size()) < a.minTargets) {
            r.targets.clear();
        }
        return r;
    }

private:
    AiAgent ai_;
    bool human_ = false;
    std::string label_;
};

}  // namespace

// ---------------------------------------------------------------- 导出给 JS
extern "C" {

// 开一局：seed 随机种子；humanSeat 0/1 = 谁来操作，-1 表示全 AI 观战；hp 起始体力；
//         baguaMode 0=自动 1=每次询问 2=关闭（【八卦阵】发动方式）
EMSCRIPTEN_KEEPALIVE
void sgs_start(int seed, int humanSeat, int hp, int baguaMode) {
    g_humanSeat = humanSeat;
    g_phase = Phase::None;
    g_useGroups.clear();
    std::string path = locateDeckFile();
    std::vector<CardProto> deck;
    std::string err;
    if (path.empty() || !loadDeckFromCsv(path, deck, &err)) {
        js_log(str("牌堆加载失败：", err.empty() ? "找不到 data/standard_cards.csv" : err).c_str());
        js_done();
        return;
    }

    g_room = std::make_unique<Room>(deck, static_cast<unsigned>(seed));
    g_room->setSilent(true);  // 浏览器里走 log sink，不再往 stdout 刷
    g_room->setLogSink([](const std::string& s) { js_log(s.c_str()); });
    g_room->setMaxTurns(300);

    // 【八卦阵】发动方式：开局值 + 之后随时可从页面菜单里改（动态读取）
    g_room->setEightDiagramMode(eightDiagramModeFromInt(baguaMode));
    g_room->setEightDiagramModeProvider([]() { return js_bagua_mode(); });
    js_log(str("【八卦阵】发动方式：", eightDiagramModeName(g_room->eightDiagramMode()),
               "（可在右上角「菜单」里随时改）")
               .c_str());

    // 所有卡牌与装备技能
    registerStandardCards(*g_room);

    // 展示事件不解析日志；回调只观察规则结果，时间控制仍在 wasm 内部。
    g_room->registerTrigger("WebPhase", Event::PhaseStart, -1, -1000,
        [](Room& r, TriggerParams& p) {
            g_phase = static_cast<Phase>(p.number);
            js_state(stateJson(r).c_str());
            return true;
        });
    for (Event e : {Event::CardUsed, Event::CardResponded, Event::Judge,
                    Event::Damaged, Event::Recover, Event::Dying,
                    Event::CardsMoved, Event::CardFinished}) {
        g_room->registerTrigger("WebPresentation", e, -1, -1000,
            [e](Room& r, TriggerParams& p) {
                if (e == Event::CardUsed) g_useGroups.push_back(++g_useSequence);
                const int group = g_useGroups.empty() ? 0 : g_useGroups.back();
                std::string type;
                if (e == Event::CardUsed) type = "use";
                else if (e == Event::CardResponded) type = "response";
                else if (e == Event::Judge) type = "judge";
                else if (e == Event::Damaged) type = "damage";
                else if (e == Event::Recover) type = "recover";
                else if (e == Event::Dying) type = "dying";
                else if (e == Event::CardFinished) type = "finish";
                else type = "move";
                std::string j = str("{\"type\":\"", type, "\",\"group\":", group, ",\"who\":", p.who,
                    ",\"amount\":", p.damage ? p.damage->amount : p.number,
                    ",\"areaIndex\":", static_cast<int>(p.toArea), ",\"area\":\"", areaName(p.toArea),
                    "\",\"reason\":\"", jsonEscape(p.judge ? p.judge->reason : p.reason), "\"");
                if (e == Event::CardUsed && p.use) j += ",\"card\":" + protoJson(p.use->proto, p.use->cardId);
                else if (e == Event::Judge && p.judge) j += ",\"card\":" + cardJson(r, p.judge->cardId);
                else if (e == Event::CardResponded && p.cardId >= 0) j += ",\"card\":" + cardJson(r, p.cardId);
                if (e == Event::CardUsed && p.use) {
                    j += ",\"targets\":[";
                    for (size_t i = 0; i < p.use->targets.size(); ++i) {
                        if (i) j += ",";
                        j += str(p.use->targets[i]);
                    }
                    j += "]";
                }
                if (e == Event::CardsMoved) {
                    j += ",\"moves\":[";
                    for (size_t i = 0; i < p.moves.size(); ++i) {
                        if (i) j += ",";
                        const auto& move = p.moves[i];
                        const bool hidden = p.toArea == Area::Hand && g_humanSeat >= 0 && p.who != g_humanSeat;
                        j += str("{\"fromOwner\":", move.fromOwner, ",\"fromArea\":", static_cast<int>(move.fromArea),
                                 ",\"card\":", cardJson(r, move.cardId, hidden), "}");
                    }
                    j += "]";
                }
                j += "}";
                js_state(stateJson(r).c_str());
                js_event(j.c_str());
                if (e == Event::CardFinished && !g_useGroups.empty()) g_useGroups.pop_back();
                if (e == Event::CardUsed || e == Event::CardResponded || e == Event::Judge ||
                    e == Event::Damaged || e == Event::Recover || e == Event::CardFinished || e == Event::CardsMoved) {
                    int ms = js_speed_ms();
                    if (ms > 0) emscripten_sleep(static_cast<unsigned>(e == Event::CardFinished ? ms / 3 : e == Event::CardsMoved ? std::min(ms, 240) : ms));
                }
                return true;
            });
    }
    // 回合开始 / 阵亡时推一次状态给页面
    g_room->registerTrigger("WebStateTurn", Event::TurnStart, -1, -1000,
                            [](Room& r, TriggerParams&) {
                                js_state(stateJson(r).c_str());
                                return true;
                            });
    g_room->registerTrigger("WebStateDie", Event::Die, -1, 1000, [](Room& r, TriggerParams&) {
        js_state(stateJson(r).c_str());
        return true;
    });

    bool h0 = (humanSeat == 0);
    bool h1 = (humanSeat == 1);
    g_room->setupPlayer(0, h0 ? "你" : "先手方", true, hp,
                        std::make_unique<WebAgent>(h0, h0 ? "你" : "先手方"));
    g_room->setupPlayer(1, h1 ? "你" : "后手方", false, hp,
                        std::make_unique<WebAgent>(h1, h1 ? "你" : "后手方"));

    pushState();
    g_room->run();  // 整局在这里跑；需要人类决策时会在 WebAgent 里挂起
    pushState();
    js_done();
}

// 让页面能问一句"现在还在跑吗"（重新开始按钮用）
EMSCRIPTEN_KEEPALIVE
int sgs_running() { return g_room && !g_room->gameOver() ? 1 : 0; }

}  // extern "C"
