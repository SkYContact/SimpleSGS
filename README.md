# 三国杀 1v1 精简引擎（C++17，无武将 / 仅标准版卡牌）

一个可直接编译运行的最小三国杀规则内核：**两名玩家、没有武将和技能、只用标准版牌堆**，
但把「回合阶段机 / 牌效果 / 事件触发器 / 询问-应答 / 判定 / 无懈链 / 濒死 / 装备技能」这些
骨架都按正式实现的做法搭好，方便后面往上加武将和扩展包。

同一份 C++ 内核既能在命令行跑，也能用 Emscripten 编成 WebAssembly 在浏览器里玩（见第七节）。

* 仓库：<https://github.com/SkYContact/SimpleSGS>
* 许可：**GPL-3.0**（见 [LICENSE](LICENSE)）

三分钟上手：

```bash
# 桌面版（Windows 现成脚本，或 cmake）
build_desktop.bat            # -> build/sgs1v1.exe
build/sgs1v1.exe --human 0   # 你和 AI 打一局

# 网页版（需要 Emscripten，装一次）
powershell -ExecutionPolicy Bypass -File web\install_emsdk.ps1
web\build_web.bat            # -> web/dist/sgs.wasm
web\serve.bat                # 打开 http://localhost:8000
```

---

## 一、参考过的开源实现

| 项目 | 语言 / 技术栈 | 从它借鉴了什么 |
|---|---|---|
| [Qsgs-Fans/FreeKill](https://github.com/Qsgs-Fans/FreeKill) | Qt Quick + C++，逻辑层 Lua | **逻辑与数据分离**（卡牌/武将数据放独立仓库）、**单线程 + 请求应答**的服务端模型、断线重连/录像回放这类"内核外"能力单独分层 |
| [Mogara/QSanguosha-v2](https://github.com/Mogara/QSanguosha-v2) | C++ / Qt Graphics View，GPLv3 | 类划分：`Room`（服务器权威房间）+ `ServerPlayer` + `Card` + `Skill/TriggerSkill`；`src/core` 放内核、`src/package` 注册卡包、`src/server` 只跑逻辑；**技能 = 监听 TriggerEvent 的回调**，`ViewAsSkill`（转化技）/`ProhibitSkill`（禁止）/`DistanceSkill`（距离修正）分类；`RoomThread` 统一处理"询问-应答" |
| 无名杀 Noname | JS/TS | 异步技能（Promise/async）的事件-响应写法、纯前端配置驱动的扩展方式 |

> 许可证提醒：上面两个主要项目是 **GPLv3**。本目录是**按它们的架构思路重写**的精简实现，
> 没有拷贝其代码；如果以后要移植它们的素材/代码，注意 GPL 传染性。

### 本项目和它们的对应关系

| QSanguosha / FreeKill 概念 | 本项目 |
|---|---|
| `Room` / `ServerPlayer` | `Room` / `Player`（牌用 id 引用，容器只存 id，与 QSanguosha 的 ServerPlayer::handcard 一致） |
| `TriggerSkill::trigger(event, room, owner, data)` | `Trigger{event, owner, priority, fn}`，任何东西都能监听事件 |
| `CardUseStruct` / `DamageStruct` | 同名结构体（`Types.h`） |
| `RoomThread` 的询问队列 | `Agent::ask()` + `Ask`/`AskResult`，AI 和人工都只是换一个 Agent |
| `Package`（卡包注册） | `registerStandardCards(room)` + 外部 `data/standard_cards.csv` |

---

## 二、目录结构

```
sgs1v1/
├── CMakeLists.txt
├── build_desktop.bat           # MSVC 一键编桌面版（不依赖 CMake）
├── data/standard_cards.csv     # 牌堆数据（107 张）；改牌堆只改这张表
├── src/
│   ├── Types.h/.cpp            # 花色/牌型/阶段/牌结构/伤害结构 等基础类型
│   ├── CardDatabase.h/.cpp     # CSV 牌堆加载
│   ├── Agent.h                 # 决策接口（Ask / AskResult）——"玩家"抽象
│   ├── AiAgent.h/.cpp          # 启发式 AI
│   ├── ConsoleAgent.h/.cpp     # 控制台人工操作
│   ├── Player.h                # 角色状态（体力/手牌/装备/判定区）
│   ├── Event.h                 # 事件表 + 触发器定义
│   ├── Room.h/.cpp             # 规则内核：回合流程、距离、伤害、濒死、判定、无懈、胜负
│   ├── CardEffects.h/.cpp      # 标准版所有牌的效果 + 装备技能（以事件监听注册）
│   └── main.cpp                # 入口 / 自对弈跑批
└── web/                        # C++ → WebAssembly（Emscripten）
    ├── wasm_api.cpp            # 胶水层：WebAgent + JSON 桥 + 导出的 sgs_start
    ├── install_emsdk.ps1       # 装 emsdk（官方源）
    ├── emsdk_fast_download.ps1 # 国内加速：8 连接分段下 668MB 工具链
    ├── build_web.bat           # 一条命令编出 wasm
    ├── serve.bat               # 起本地静态服务器
    ├── check_frontend.js       # 前端静态检查（id / data-* 钩子 / 语法）
    ├── test_node.js            # 引擎冒烟测试（AI 观战 / 模拟玩家点击）
    ├── test_frontend.js        # 前端端到端测试（迷你 DOM + 真实 app.js + 真实 wasm）
    └── dist/                   # 网页 + 编译产物（sgs.js / sgs.wasm / sgs.data）
        ├── index.html
        ├── style.css
        └── app.js
```

---

## 三、编译运行

```bash
cd sgs1v1
cmake -S . -B build
cmake --build build --config Release
./build/sgs1v1              # 两 AI 自动打一局（简略输出）
```

也可以单文件手编（不依赖 CMake）：

```bash
g++ -std=c++17 -O2 -Isrc src/*.cpp -o sgs1v1
```

常用参数：

```bash
sgs1v1 --human 0            # 你操作座位 0，AI 操作座位 1（完整过程日志）
sgs1v1 --games 200 --seed 1 # 跑 200 局自对弈，输出先手/后手胜率与平均回合
sgs1v1 --hp 5 --max-turns 300
sgs1v1 --deck ../data/standard_cards.csv
```

> Windows 控制台若中文/符号乱码：程序已调用 `SetConsoleOutputCP(CP_UTF8)`，
> 建议在 Windows Terminal / PowerShell 7 下运行；MSVC 编译已加 `/utf-8`。

自带一个用 MSVC 编出来的可执行文件在 `build/sgs1v1.exe`，可以直接跑：

```powershell
cd sgs1v1
.\build\sgs1v1.exe                 # 单局完整过程（两 AI）
.\build\sgs1v1.exe --games 300     # 批量自对弈统计
```

已做过的自对弈回归（`--games 300 --seed 11`）：**无崩溃、无死循环、无平局**，
先手胜率 59.3% / 后手 40.7%，平均 20.4 回合；装备与延时锦囊的效果
（闪电命中、乐不思蜀跳过出牌、八卦阵改判出闪、丈八蛇矛转化、麒麟弓弃马、
青龙偃月刀连杀、方天画戟多目标、仁王盾/青釭剑、无懈链）都被实际触发过。

---

## 四、已经实现的规则

**流程**：开局双方各摸 4 张起始手牌，之后
`回合开始 → 判定 → 摸牌(2) → 出牌 → 弃牌(手牌上限=体力) → 回合结束`，
一方阵亡即结束（同归于尽算平局）。

**卡牌**（标准版 107 张）

* 基本牌：杀（每回合 1 次，含攻击范围判定）、闪、桃（含濒死救援）
* 锦囊：过河拆桥、顺手牵羊（距离 ≤1，会被防守马挡）、无中生有、决斗（轮流打【杀】）、
  借刀杀人（1v1 下指定对方攻击你）、桃园结义、南蛮入侵、万箭齐发、五谷丰登（亮存活人数张牌依次选）、
  无懈可击（见下）、乐不思蜀、闪电（判定转移 + 3 点雷电伤害）
* 装备：8 种武器 + 八卦阵 + 仁王盾 + 4 种坐骑

**装备技能**：诸葛连弩（无限【杀】）、青釭剑（无视防具）、雌雄双股剑（异性目标弃牌/摸牌）、
青龙偃月刀（被闪后再杀）、丈八蛇矛（两张手牌当【杀】）、贯石斧（弃两张牌强制命中）、
方天画戟（最后一张手牌可多指定两个目标）、麒麟弓（伤害后弃坐骑）、八卦阵（判定红则视为【闪】）、
仁王盾（黑色【杀】无效）、坐骑（距离 ±1，会实际影响攻击范围与【顺手牵羊】）。

**无懈可击**：铁索式奇偶结算——A 出无懈抵消，B 可以再无懈把它顶回来，一直链到没人出为止。
每张锦囊**对每个目标分别询问**（和正式规则一致）。

---

## 五、刻意做的简化（和正式规则的差异）

1. **没有武将、没有身份、只有 2 人**：先手固定为座位 0，双方体力 4（`--hp` 可改），没有主公加成与奖惩。
2. 性别固定为「座位 0 男 / 座位 1 女」，只为让【雌雄双股剑】有意义。
3. **八卦阵是自动触发的**：只有当手里没有【闪】时才会去判定（正式规则下玩家可以自己选择是否发动）。
4. 无懈的询问顺序是「从锦囊使用者下家开始绕一圈」，没有做技能插入的时序细分。
5. 没有"使用牌时"的时序细分（如"成为目标时/指定目标时"的先后），只分了 `CardUsed → CardEffect` 两个介入点。
6. 没有技能、没有觉醒/限定技、没有改判（`JudgeStruct` 里留了 `retrialCards` 的位置）。
7. 单线程、无网络：所有玩家都在同一个进程里，靠 `Agent` 区分是 AI 还是人。

---

## 六、怎么写自己的卡 / 技能

**加一张牌**：`data/standard_cards.csv` 加一行，再在 `CardEffects.cpp` 的
`applyCardEffect()` 里加一个分支（单目标效果），需要新询问的话用 `room.askAgent(Ask{...})`。
如果要挂监听，用：

```cpp
room.registerTrigger("MySkill", Event::Damaged, owner, priority,
                     [](Room& r, TriggerParams& p) {
                         // p.damage / p.use / p.who ...
                         return true;   // 返回 false 会中断该事件分发
                     });
```

**加一个武将**：本质上就是在开局时按武将名注册一批 `registerTrigger(...)`，
并在 `Event::AskForCard`、`Event::CardUsed`、`Event::CardEffect`、`Event::Damage/Damaged`、
`Event::Dying` 这些点上写回调；需要"转化技"就在 `buildPlayOptions()` 里多塞一个 `PlayOption`
（丈八蛇矛就是 20 行代码的样板）。

**换成网络对战**：把 `Agent` 换成"发请求给远端、等回包"的实现即可，
`Room` 一行都不用改——这正是它把决策抽成接口的原因。

---

## 七、C++ → WebAssembly：浏览器里跑真实结算

同一份 `src/` 内核用 **Emscripten** 编成 wasm，网页作为"玩家"接进来，
规则、牌堆、随机数全部还是 C++ 在跑（不是 JS 重写一遍）。

```
浏览器                        wasm (C++)                       JS
────────────────────────────────────────────────────────────────────────
点「人机对战」  ──►  _sgs_start(seed, seat, hp)
                      Room::run() 主循环
                        └─ WebAgent::ask()
                             ① js_ask(询问JSON)  ──►  sgsOnAsk() 渲染按钮
                             ② emscripten_sleep(30) 挂起 ⟲ 等点击
       点按钮  ──►  window.sgsAnswer = {...}  （只写 JS 全局，不回调 wasm）
                             ③ 醒来读走答案，继续结算
                      Room::chat()  ──► js_log()  ──►  sgsOnLog() 日志面板
                      TurnStart / 每次询问 ──► js_state() ──► sgsOnState() 刷新棋盘
本局结束 ──► js_done() ──► sgsOnDone() 结算提示
```

关键设计（都在 `web/wasm_api.cpp`）：

1. **决策挂起**：`WebAgent::ask()` 用 `emscripten_sleep`（Asyncify）把 C++ 的调用栈
   冻结、把控制权还给浏览器；玩家点完按钮、页面把结果写进 `window.sgsAnswer`，
   C++ 醒来读走接着跑。不需要把 `run()` 拆成状态机。
2. **绝不从 JS 回调 wasm**：Asyncify 挂起期间模块不能再被调用（会报
   "cannot call into wasm while it is sleeping"），所以按钮只写 JS 全局变量，
   由 C++ 轮询读取——这是这套方案能稳的关键。
3. **观战 = 同一套代码**：`WebAgent` 在非人类座位直接调 `AiAgent`，只是每次决策
   `sleep(speed)` 做播放节奏，所以"AI 观战"和"人机对战"是同一个二进制。
4. 内核只多了一个 `Room::setLogSink()`（日志外送），桌面版完全不受影响。

### 编译

```powershell
# 1) 装 Emscripten SDK（wasm-binaries.zip 约 668MB，只需一次）
powershell -ExecutionPolicy Bypass -File web\install_emsdk.ps1
#    国内单连接约 0.8MB/s（~14 分钟）。嫌慢就用这个 8 连接分段版（实测 <5 分钟，
#    且可反复重跑/断点续传），它会自动接着装好并 activate：
powershell -ExecutionPolicy Bypass -File web\emsdk_fast_download.ps1

# 2) 编译
web\build_web.bat
# 产物: web\dist\sgs.js + sgs.wasm + sgs.data（牌堆 csv 被 --preload-file 打进虚拟 FS）
```

等价的手写命令（注意用 **em++** 而不是 emcc，C++ 项目用 emcc 链接会一堆 undefined symbol）：

```bash
em++ -O1 -std=c++17 -I src \
  src/Types.cpp src/CardDatabase.cpp src/Room.cpp src/CardEffects.cpp src/AiAgent.cpp \
  web/wasm_api.cpp \
  --preload-file data/standard_cards.csv@data/standard_cards.csv \
  --no-entry -sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 \
  -sEXPORT_NAME=createSgsModule -sENVIRONMENT=web,node -sSTACK_SIZE=1048576 \
  -o web/dist/sgs.js
```

踩过的两个坑，写下来免得再撞：

* **必须 `-O1`**：`-O2/-O3` 时 binaryen 的 `--asyncify` pass 会报
  `Module::getGlobal: __asyncify_state does not exist`（本 emsdk 的 binaryen 与
  `--strip-target-features` 组合有 bug）。
* **EM_JS 的 JS 体里别写空字符常量 `''`**：clang 会把 JS 体按 C++ 词法扫一遍，
  `''` 会触发 `-Winvalid-pp-token: empty character constant`，用 `""` 替代。
* **重新编译前要让旧的 `sgs.wasm` 可写**：binaryen 的 `--asyncify` 是就地改写，
  浏览器预览页 / 静态服务器占着这个文件时 `wasm-opt` 会直接崩（`0xC0000005`）。
  `build_web.bat` 已经加了"先删旧产物、删不掉就报错退出"的防御。

### 跑起来看

```powershell
web\serve.bat          # 等价于 python -m http.server 8000 --directory web\dist
# 然后浏览器打开 http://localhost:8000
```

> wasm 必须走 http（`file://` 会被浏览器拦掉），所以需要上面这个静态服务器。
> `serve.py` 会带 `Cache-Control: no-store`——用 `python -m http.server` 的话，
> 改了 `app.js` / `sgs.wasm` 之后浏览器常常还拿旧缓存，表现就是"改了没反应"。
> 另外脚本会自动用 `py -3` 定位真实 python（PATH 里的 `python` 可能是 Microsoft Store 假壳）。

页面上可以：**人机对战（先手/后手）**、**AI 观战**、调播放速度、随时「托管 AI」、
看实时棋盘（体力/手牌/装备/判定区/距离/攻击范围）和逐条对局日志。

### 不开浏览器也能验证

编译产物是 `ENVIRONMENT=web,node` 的（一份 wasm 两边通用），所以下面三个测试
跑的都是**浏览器里那一份一模一样的代码**：

```powershell
node web\check_frontend.js          # 静态检查：语法 / DOM id / data-* 钩子
node web\test_node.js spectate 2    # 引擎层：AI 观战 2 局
node web\test_node.js human 1       # 引擎层：模拟玩家点击 1 局（验证挂起-唤醒）
node web\test_frontend.js           # 端到端：迷你 DOM + 真实 app.js + 真实 wasm 打完一局
```

`test_frontend.js` 会解析 `index.html` 真实 id、把 `dist/app.js` 原样加载、
然后模拟点击按钮驱动整局（单选直接点、多选要点「确认」、随机种子与「换一个」也一起验），
最后检查棋盘渲染字符数、日志行数、`console.error`。

> **写这类"挂起等待"的测试要注意**：Asyncify 挂起时 `_sgs_start` 会立刻返回、
> 整局在异步继续，所以不能再同步调进去（等于在挂起中重入，会卡死），
> 只能用 `sgsOnDone` 回调驱动下一局；另外别用轮询（长局会被轮询间隔拖死），
> 在 `sgsOnAsk` 里直接排一次点击即可。

---

## 八、许可

本项目采用 **GNU General Public License v3.0**，全文见 [LICENSE](LICENSE)。

意思是：你可以自由使用、修改、分发（包括商用），但只要对外分发修改版，
就必须同样以 GPLv3 开源。

参考的两个主要开源实现 [QSanguosha-v2](https://github.com/Mogara/QSanguosha-v2)、
[FreeKill](https://github.com/Qsgs-Fans/FreeKill) 也都是 GPLv3；
本仓库是按它们的**架构思路重写**的，不包含它们的代码与美术素材。
《三国杀》是游卡桌游的商标与版权作品，本仓库只用于技术学习，不含任何官方素材。

