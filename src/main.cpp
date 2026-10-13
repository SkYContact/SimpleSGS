#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "AiAgent.h"
#include "CardDatabase.h"
#include "CardEffects.h"
#include "ConsoleAgent.h"
#include "Room.h"

using namespace sgs;

namespace {

struct Options {
    unsigned seed = 20261013u;
    int human = -1;        // 哪个座位由人操作（-1 = 全 AI）
    int hp = 4;            // 起始体力（无武将，双方相同）
    int games = 1;         // 自对弈局数
    int maxTurns = 200;    // 防平局死循环
    std::string deck;      // 牌堆数据文件
    std::string bagua = "ask";   // 【八卦阵】发动方式：auto / ask / off（默认 ask＝正式规则）
    bool quiet = false;
};

void usage() {
    std::cout <<
        "三国杀 1v1 精简引擎（无武将、标准版卡牌）\n"
        "用法: sgs1v1 [选项]\n"
        "  --human <0|1>   指定座位由你操作（默认全 AI）\n"
        "  --games <N>     自对弈局数（默认 1）\n"
        "  --hp <N>        起始体力（默认 4）\n"
        "  --seed <N>      随机种子\n"
        "  --max-turns <N> 单局回合上限（默认 200）\n"
        "  --deck <path>   牌堆 csv 路径\n"
        "  --bagua <mode>  【八卦阵】发动方式：ask(默认，每次需要【闪】先问是否发动) / auto(没手牌闪才判) / off(从不)\n"
        "  --quiet         不打印对局过程（跑批用）\n";
}

bool matchArg(const char* a, const char* name) { return std::strcmp(a, name) == 0; }

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Options opt;
    for (int i = 1; i < argc; ++i) {
        if (matchArg(argv[i], "--help") || matchArg(argv[i], "-h")) {
            usage();
            return 0;
        } else if (matchArg(argv[i], "--human") && i + 1 < argc) {
            opt.human = std::atoi(argv[++i]);
        } else if (matchArg(argv[i], "--games") && i + 1 < argc) {
            opt.games = std::atoi(argv[++i]);
        } else if (matchArg(argv[i], "--hp") && i + 1 < argc) {
            opt.hp = std::atoi(argv[++i]);
        } else if (matchArg(argv[i], "--seed") && i + 1 < argc) {
            opt.seed = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        } else if (matchArg(argv[i], "--max-turns") && i + 1 < argc) {
            opt.maxTurns = std::atoi(argv[++i]);
        } else if (matchArg(argv[i], "--deck") && i + 1 < argc) {
            opt.deck = argv[++i];
        } else if (matchArg(argv[i], "--bagua") && i + 1 < argc) {
            opt.bagua = argv[++i];
        } else if (matchArg(argv[i], "--quiet")) {
            opt.quiet = true;
        } else {
            std::cout << "未知参数: " << argv[i] << "\n";
            usage();
            return 1;
        }
    }
    if (opt.games < 1) opt.games = 1;
    if (opt.hp < 1) opt.hp = 4;

    EightDiagramMode bagua = EightDiagramMode::Auto;
    if (!parseEightDiagramMode(opt.bagua, bagua)) {
        std::cout << "无效的 --bagua 取值: " << opt.bagua << "（可选 auto / ask / off）\n";
        return 1;
    }

    if (opt.deck.empty()) opt.deck = locateDeckFile();
    if (opt.deck.empty()) {
        std::cout << "找不到牌堆数据 data/standard_cards.csv，请用 --deck 指定路径。\n";
        return 1;
    }

    std::vector<CardProto> deck;
    std::string err;
    if (!loadDeckFromCsv(opt.deck, deck, &err)) {
        std::cout << "牌堆加载失败: " << err << "\n";
        return 1;
    }
    if (!opt.quiet) {
        std::cout << "牌堆: " << opt.deck << "（" << deck.size() << " 张牌）\n";
        std::cout << "【八卦阵】发动方式: " << eightDiagramModeName(bagua) << "\n";
        if (opt.human >= 0) {
            std::cout << "你是座位 " << opt.human << "，输入数字选择，0 或 q 表示放弃。\n";
        }
    }

    int win0 = 0, win1 = 0, draw = 0, totalTurns = 0;

    for (int g = 0; g < opt.games; ++g) {
        // 单局默认打印完整过程；跑批/多局只打印结果
        bool quiet = opt.quiet || opt.games > 1;
        Room room(deck, opt.seed + static_cast<unsigned>(g) * 7919u);
        room.setSilent(quiet);
        room.setMaxTurns(opt.maxTurns);
        room.setEightDiagramMode(bagua);

        // 卡牌与装备技能都通过事件监听注册（以后加武将技能就是在这里再注册一批）
        registerStandardCards(room);

        bool h0 = opt.human == 0;
        bool h1 = opt.human == 1;
        std::unique_ptr<Agent> a0 = h0 ? std::unique_ptr<Agent>(new ConsoleAgent("你"))
                                       : std::unique_ptr<Agent>(new AiAgent("先手AI"));
        std::unique_ptr<Agent> a1 = h1 ? std::unique_ptr<Agent>(new ConsoleAgent("你"))
                                       : std::unique_ptr<Agent>(new AiAgent("后手AI"));

        room.setupPlayer(0, h0 ? "你" : "先手方", true, opt.hp, std::move(a0));
        room.setupPlayer(1, h1 ? "你" : "后手方", false, opt.hp, std::move(a1));

        int w = room.run();
        totalTurns += room.turnCount();

        if (opt.games > 1) {
            // 多局模式只输出一行结果
            std::cout << "#" << (g + 1) << " 胜者: ";
            if (w >= 0) {
                std::cout << room.playerName(w);
            } else {
                std::cout << "平局";
            }
            std::cout << "（" << room.turnCount() << " 回合）\n";
        }

        if (w == 0) ++win0;
        else if (w == 1) ++win1;
        else ++draw;
    }

    if (opt.games > 1) {
        std::cout << "\n=== 统计（" << opt.games << " 局）===\n";
        std::cout << "先手方胜: " << win0 << " (" << (100.0 * win0 / opt.games) << "%)\n";
        std::cout << "后手方胜: " << win1 << " (" << (100.0 * win1 / opt.games) << "%)\n";
        std::cout << "平局    : " << draw << "\n";
        std::cout << "平均回合: " << (1.0 * totalTurns / opt.games) << "\n";
    }
    return 0;
}
