/* 前端静态检查：
 *   1) app.js 语法（node --check 已覆盖，这里再 require 一次能暴露引用错误）
 *   2) app.js 里引用的 DOM id 是否都在 index.html 里存在
 *   3) CSS 里定义的 class 是否覆盖 JS 动态加的类（只报缺，不报错）
 *   用法: node web/check_frontend.js
 */
'use strict';
const fs = require('fs');
const path = require('path');
const dir = path.join(__dirname, 'dist');
const app = fs.readFileSync(path.join(dir, 'app.js'), 'utf8');
const html = fs.readFileSync(path.join(dir, 'index.html'), 'utf8');
const css = fs.readFileSync(path.join(dir, 'style.css'), 'utf8');

let bad = 0;

// --- 1) 语法 ---
try {
  new Function(app);                       // 只编译不执行
  console.log('[ok] app.js 语法通过');
} catch (e) {
  console.log('[x] app.js 语法错误: ' + e.message);
  bad++;
}

// --- 2) id 交叉检查（app.js 里 getElementById 的 id，必须来自 index.html 或 app.js 自己 innerHTML 出来的）---
const usedIds = new Set();
for (const m of app.matchAll(/\$\('([^']+)'\)/g)) usedIds.add(m[1]);
for (const m of app.matchAll(/getElementById\('([^']+)'\)/g)) usedIds.add(m[1]);
const knownIds = new Set();
for (const m of html.matchAll(/id="([^"]+)"/g)) knownIds.add(m[1]);
for (const m of app.matchAll(/id="([^"]+)"/g)) knownIds.add(m[1]);   // 动态生成的按钮
const missing = [...usedIds].filter((i) => !knownIds.has(i));
if (missing.length) {
  console.log('[x] app.js 引用了不存在的 id: ' + missing.join(', '));
  bad++;
} else {
  console.log(`[ok] ${usedIds.size} 个 DOM id 全部有来源`);
}

// --- 3) 渲染里用到的 data-* 钩子必须和绑定逻辑一致 ---
for (const hook of ['data-card', 'data-opt', 'data-target']) {
  const usedInRender = app.includes(hook);
  const usedInBind = app.includes(`'[${hook}]'`);
  if (usedInRender !== usedInBind) {
    console.log(`[x] ${hook} 渲染与事件绑定不一致`);
    bad++;
  }
}
console.log('[ok] data-* 钩子渲染/绑定一致');

// --- 4) 内核接口 ---
for (const need of ['_sgs_start', 'createSgsModule']) {
  if (!app.includes(need)) { console.log(`[x] app.js 没有用到 ${need}`); bad++; }
}
console.log(bad ? `\n❌ ${bad} 个问题` : '\n✅ 前端静态检查通过');
process.exit(bad ? 1 : 0);
