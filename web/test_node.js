/* ============================================================================
 *  node 冒烟测试：不开浏览器验证 wasm 里的引擎 + Asyncify 挂起/继续链路
 *
 *  用法:
 *    node web/test_node.js                # AI 观战一局
 *    node web/test_node.js human 2        # 模拟玩家点击（验证挂起-唤醒）
 *    node web/test_node.js spectate 3 500 # 观战 3 局，每步 500ms（走异步 sleep 路径）
 *
 *  注意：Asyncify 挂起时 _sgs_start 会立刻返回，整局还在异步跑。
 *        所以只能用"回调驱动"（sgsOnDone 里再开下一局），不能再同步调进去。
 * ========================================================================== */
'use strict';

const path = require('path');

const mode = process.argv[2] || 'spectate';        // spectate | human
const games = parseInt(process.argv[3] || '1', 10);
const speed = parseInt(process.argv[4] || '0', 10);

const distDir = path.join(__dirname, 'dist');
process.chdir(distDir);                            // .wasm/.data 按 cwd 找
const factory = require(path.join(distDir, 'sgs.js'));

let logs = 0, asks = 0, states = 0, finished = 0, leaks = 0;
let g = 0, lastState = null, mod = null;
let watchdog = null;

function resetGlobals() {
  logs = asks = states = 0;
  globalThis.sgsSpeedMs = speed;
  globalThis.sgsAutoPlay = false;
  globalThis.sgsAnswer = { ready: false, option: -1, cards: [], targets: [] };

  globalThis.sgsOnLog = (s) => {
    logs++;
    // 内核自己会做"牌数守恒"自检：任何效果把牌吃掉都会打这行，测试直接判失败。
    // （五谷丰登亮牌曾经用 Area::None，选牌被 moveCards 静默跳过、剩余亮牌也丢了）
    if (s.includes('牌数守恒检查失败')) leaks++;
    if (speed > 0 && /^===/.test(s)) console.log(s);
  };
  globalThis.sgsOnState = (json) => { states++; lastState = JSON.parse(json); };
  globalThis.sgsOnDone = () => {
    finished++;
    const st = lastState || {};
    console.log(`局#${g + 1} 结束: 胜者=${st.winner} 回合=${st.turn} ` +
                `日志=${logs} 玩家询问=${asks} 状态推送=${states}`);
    g++;
    if (g < games) setTimeout(startOne, 0);        // 必须异步再开下一局
    else finish();
  };
  globalThis.sgsOnAsk = (json) => {                // 模拟玩家点击
    asks++;
    const a = JSON.parse(json);
    const ans = { ready: true, option: -1, cards: [], targets: [] };
    if (a.kind === 'card' && a.cards.length) ans.cards = [a.cards[0].id];
    else if (a.kind === 'multi' && a.cards.length >= a.minCards) {
      ans.cards = a.cards.slice(0, a.minCards).map((c) => c.id);
    } else if (a.kind === 'option' && a.options.length) ans.option = 0;
    else if (a.kind === 'target' && a.targets.length >= Math.max(1, a.minTargets)) {
      ans.targets = a.targets.slice(0, Math.max(1, a.minTargets)).map((t) => t.id);
    }
    if (asks <= 5) console.log(`  [ask#${asks}] ${a.prompt}`);
    globalThis.sgsAnswer = ans;
  };
}

function startOne() {
  resetGlobals();
  const seat = mode === 'human' ? 0 : -1;
  mod._sgs_start(1000 + g, seat, 4);               // 可能立刻返回，整局在异步继续
}

function finish() {
  if (watchdog) clearTimeout(watchdog);
  console.log(`\n汇总: ${games} 局, 完成回调 ${finished} 次, 丢牌告警 ${leaks} 次`);
  const ok = finished === games && leaks === 0;
  if (leaks) console.log('❌ 有牌被效果吃掉了（牌数守恒自检报警）');
  console.log(ok ? '✅ 通过' : '❌ 异常');
  process.exit(ok ? 0 : 1);
}

factory().then((m) => {
  mod = m;
  watchdog = setTimeout(() => {
    console.error(`❌ 超时：${finished}/${games} 局完成，疑似挂起链路有问题`);
    process.exit(1);
  }, 60000);
  startOne();
}).catch((e) => {
  console.error('加载 wasm 失败:', e);
  process.exit(1);
});
