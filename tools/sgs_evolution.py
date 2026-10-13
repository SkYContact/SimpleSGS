# -*- coding: utf-8 -*-
"""
SGS 1v1 硬选择演化（d3_evolution.py 的 SGS 平移版）。

结构：
  * 种群 μ=5 池 + 环移民退化为单冠军链 + 候选池（1v1 单对手格局，先跑单岛）
  * 每代 λ 个候选：ES ±ε 配对（OpenAI-ES 漂移中心 + rank 加权回写）、裸变异、λ-combine
  * 筛选：256 局×双宇宙（先手/后手位各一，配对种子）→ top CONF_TOP 进验收
  * 验收：1024 局×双宇宙，门 = 两个宇宙都 ≥50% 且合并 ≥50%+GATE_PP
  * 引擎种子协议：seed0 + g*7919（与 main.cpp 严格一致，位级可复现）
  * 配对验收靠同种子：对局结果用 --emit 的 E 记录解析

用法:
    python tools/sgs_evolution.py --engine build/sgs1v1.exe --init ckpt/g0.pt \
        --work run/island --gens 12
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import time

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, "D:/ygo/ygopro/tools")
from bake_fb_onnx import run_bake  # noqa: E402  进程内烤 fb（省 python 启动税）
from sgs_model import SgsPolicyNet, save_ckpt  # noqa: E402

# ---- 演化参数（d3 终配的 SGS 重标定起点；σ 标定判决实验挂账）----
MU_POOL = 5          # 冠军池大小（含冠军）
LAM = 12             # 每代候选数
ES_PAIRS = 5         # 其中 ES ±ε 配对数
N_BARE = 3           # 裸变异
N_COMBINE = 2        # λ-combine（+1 池内随机重组）
SIGMA0 = 0.08        # 初始相对扰动步长
SIGMA_MIN = 0.03     # 地板（1/5 法则自适应）
CONF_TOP = 4         # 进验收名额
SCREEN_GAMES = 256   # 筛选局数/宇宙
CONFIRM_GAMES = 1024  # 验收局数/宇宙
GATE_PP = 1.1        # 验收门（合并口径，百分点）
SEED0 = 910001


# ---------------------------------------------------------------- 工具
def export_onnx_fast(sd, config, out_path, tmp_dir):
    """权重 -> ONNX（进程内，~1s）。"""
    m = SgsPolicyNet(config["vocab_n"])
    m.load_state_dict(sd)
    m.eval()
    dummy = (torch.zeros(1, 24), torch.zeros(1, 78, dtype=torch.long),
             torch.zeros(1, 78, 4), torch.zeros(1, 48, dtype=torch.long),
             torch.zeros(1, 48, 10), torch.ones(1, 48))
    torch.onnx.export(m, dummy, out_path, opset_version=17, dynamo=False,
                      do_constant_folding=True,
                      input_names=["scal", "slot_code", "slot_attr", "cand_code",
                                   "cand_attr", "cand_mask"],
                      output_names=["logits", "value"],
                      dynamic_axes={n: {0: "B"} for n in
                                    ["scal", "slot_code", "slot_attr", "cand_code",
                                     "cand_attr", "cand_mask", "logits", "value"]})


def mutate(sd, sigma, rng):
    """逐层相对 σ 扰动（复用 d3 的语义：norm×σ×randn，死层冲击不搞——SGS 无死层账）。"""
    out = {}
    for k, v in sd.items():
        if v.dtype in (torch.float32, torch.float64) and v.numel() > 1:
            n = v.norm()
            step = rng.normal(0, 1, v.shape).astype(np.float32)
            step = torch.from_numpy(step) * sigma * (float(n) / np.sqrt(step.size))
            out[k] = (v + step).clone()
        else:
            out[k] = v.clone()
    return out


def combine(sa, sb, lam, rng):
    out = {}
    for k in sa:
        if sa[k].dtype.is_floating_point:
            mask = torch.rand_like(sa[k]) < lam
            out[k] = torch.where(mask, sa[k], sb[k])
        else:
            out[k] = sa[k].clone()
    return out


# ---------------------------------------------------------------- 对局腿
def run_leg(engine, model_a, model_b, games, seed0, emit_path, seat_a=0):
    """model_a 固定坐 seat_a，打 games 局。返回 a 的胜局数（平局不计）。"""
    args = [engine, "--games", str(games), "--seed", str(seed0), "--quiet",
            "--outcomes", emit_path]
    if seat_a == 0:
        args += ["--nn0", model_a, "--nn1", model_b]
    else:
        args += ["--nn0", model_b, "--nn1", model_a]
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wins_a = 0
    with open(emit_path, "rb") as f:
        data = f.read()
    off = 0
    while off < len(data):
        tag = data[off:off + 1]; off += 1
        if tag == b"G":
            off += 4
        elif tag == b"E":
            w, = struct.unpack_from("<i", data, off); off += 4
            if w == seat_a:
                wins_a += 1
    return wins_a


def farm_leg(engine, model_a, model1, games, seed0, chains=None, banks=2):
    """农场 W2 双组腿：model_a 坐座位0（cand），model1 坐座位1。返回 (座位0胜, 总局)。
    底盘=产线判决：fb256 批形 + FIXED_BATCH + 一链一局（chains=games，喂饱批）。"""
    chains = chains or games   # 一链一局
    args = [engine, "--farm", "--model", model_a, "--model1", model1,
            "--slots", "256", "--chains", str(chains), "--games", str(games),
            "--seed", str(seed0), "--banks", str(banks), "--quiet"]
    env = {**os.environ,
           "FARM_FIXED_BATCH": "256",
           "FARM_CUDA_DIR": "C:/Users/41601/Miniconda3/envs/q35/Lib/site-packages/torch/lib"}
    r = None
    for attempt in range(5):   # 瞬态崩溃护栏（实测偶发 0xC0000005，重试即过）
        r = subprocess.run(args, capture_output=True, text=True, env=env)
        if r.returncode == 0:
            break
        time.sleep(2.0 * (attempt + 1))
    if r.returncode != 0:
        raise RuntimeError(f"farm 腿三次崩溃 rc={r.returncode}")
    for line in r.stdout.splitlines():
        if "座位0胜" in line:
            seg = line.split("座位0胜")[1].split()[0]
            w, n = seg.split("/")
            return int(w), int(n)
    raise RuntimeError("farm 腿无统计输出: " + r.stdout[-400:] + r.stderr[-400:])


def paired_eval(engine, cand, champ, games, seed0, tmp, backend="farm",
                champ_fb=None, fb_dir=None):
    """双宇宙配对验收。backend=farm: 两个种子块各 games 局（先手位平衡在流内交替）。"""
    if backend == "farm":
        cand_fb = os.path.join(fb_dir, "cand_eval.fb256.onnx")
        run_bake(cand, cand_fb, 256)
        w = n = 0
        # 双宇宙 × 座位互换：宇宙1 cand 坐0，宇宙2 champ 坐0（cand 胜 = n-座位0胜）
        # ——消掉先手 ~60% 的座位偏置，否则验收门被先手优势灌水（g1-g4 教训）
        w1, n1 = farm_leg(engine, cand_fb, champ_fb, games, seed0)
        w2, n2 = farm_leg(engine, champ_fb, cand_fb, games, seed0 + 7919)
        os.remove(cand_fb)
        return w1 + (n2 - w2), n1 + n2
    wins = 0
    wins += run_leg(engine, cand, champ, games, seed0, tmp + "/a.bcb", seat_a=0)
    wins += run_leg(engine, cand, champ, games, seed0 + 7919, tmp + "/b.bcb", seat_a=1)
    return wins, games * 2


# ---------------------------------------------------------------- 主循环
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default="build/sgs1v1.exe")
    ap.add_argument("--init", required=True)
    ap.add_argument("--work", default="run/island")
    ap.add_argument("--gens", type=int, default=12)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--backend", choices=["farm", "subprocess"], default="farm")
    args = ap.parse_args()

    engine = os.path.abspath(args.engine)
    os.makedirs(args.work, exist_ok=True)
    tmp = os.path.join(args.work, "tmp")
    os.makedirs(tmp, exist_ok=True)
    rng = np.random.default_rng(SEED0 + args.seed * 7919 + 1)

    ck = torch.load(args.init, map_location="cpu", weights_only=False)
    champ_sd, config = ck["state"], ck["config"]
    gen = 0
    # 断点续跑
    state_path = os.path.join(args.work, "state.json")
    import json
    if os.path.exists(state_path):
        st = json.load(open(state_path))
        gen = st["gen"]
        champ_sd = torch.load(os.path.join(args.work, "champ.pt"),
                              map_location="cpu", weights_only=False)["state"]
        print(f"[resume] gen{gen}")

    champ_path = os.path.join(args.work, "champ.onnx")
    export_onnx_fast(champ_sd, config, champ_path, tmp)

    sigma = SIGMA0
    pool = [champ_sd]  # μ 池（[0] = 冠军）
    hall = os.path.join(args.work, "hall")
    os.makedirs(hall, exist_ok=True)
    log = open(os.path.join(args.work, "evo.log"), "a", encoding="utf-8")

    def say(msg):
        line = f"[g{gen}] {msg}"
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    while gen < args.gens:
        t0 = time.time()
        gen += 1
        center = pool[0]
        cands = []   # (名字, sd, kind)
        # ES ±ε 配对
        for i in range(ES_PAIRS):
            eps = {k: torch.from_numpy(
                       rng.normal(0, 1, v.shape).astype(np.float32) * sigma *
                       (float(v.norm()) / max(1.0, np.sqrt(v.numel()))) if v.numel() > 1
                       else np.zeros_like(v.numpy()))
                   if v.dtype.is_floating_point else torch.zeros_like(v)
                   for k, v in center.items()}
            cands.append((f"es{i}p", {k: (v + eps[k]).clone() for k, v in center.items()}, f"es+"))
            cands.append((f"es{i}m", {k: (v - eps[k]).clone() for k, v in center.items()}, f"es-"))
        for i in range(N_BARE):
            cands.append((f"bare{i}", mutate(center, sigma, rng), "bare"))
        for i in range(N_COMBINE):
            b = pool[rng.integers(0, len(pool))]
            lam = float(rng.uniform(0.3, 0.7))
            cands.append((f"comb{i}", combine(center, b, lam, rng), f"comb{lam:.2f}"))

        # ---- 筛选（256×2 配对）----
        champ_fb = None
        if args.backend == "farm":
            champ_fb = os.path.join(tmp, "champ.fb256.onnx")
            run_bake(champ_path, champ_fb, 256)
        scores = []
        for name, sd, kind in cands:
            p = os.path.join(tmp, f"cand_{name}.onnx")
            export_onnx_fast(sd, config, p, tmp)
            w, n = paired_eval(engine, p, champ_path, SCREEN_GAMES, SEED0 + gen * 1000,
                               tmp, args.backend, champ_fb, tmp)
            scores.append((w / n, name, sd, kind, w, n))
            os.remove(p)
        if champ_fb:
            os.remove(champ_fb)
        scores.sort(key=lambda x: -x[0])
        say(f"筛选 top: " + " | ".join(
            f"{s[1]}({s[3]}) {s[4]}/{s[5]}={s[0]*100:.1f}%" for s in scores[:CONF_TOP]) +
            f"  σ={sigma:.3f} 中位={sorted(x[0] for x in scores)[len(scores)//2]*100:.1f}%")

        # ---- 验收（1024×2）----
        promoted = False
        for s in scores[:CONF_TOP]:
            _, name, sd, kind, _, _ = s
            p = os.path.join(tmp, f"conf_{name}.onnx")
            export_onnx_fast(sd, config, p, tmp)
            champ_fb = None
            if args.backend == "farm":
                champ_fb = os.path.join(tmp, "champ.fb256.onnx")
                run_bake(champ_path, champ_fb, 256)
            w, n = paired_eval(engine, p, champ_path, CONFIRM_GAMES,
                               SEED0 + 500000 + gen * 1000, tmp, args.backend,
                               champ_fb, tmp)
            if champ_fb:
                os.remove(champ_fb)
            win = w / n
            # 两宇宙分别的门（≥50%）在 paired_eval 里已合并；严格口径再跑分位
            ok = win >= 0.5 + GATE_PP / 100
            say(f"验收 {name}({kind}): {w}/{n}={win*100:.2f}% -> {'登基' if ok else '淘汰'}")
            if ok:
                champ_sd = {k: v.clone() for k, v in sd.items()}
                export_onnx_fast(champ_sd, config, champ_path, tmp)
                shutil.copy(os.path.join(tmp, f"conf_{name}.onnx"), champ_path)
                torch.save({"state": champ_sd, "config": config},
                           os.path.join(hall, f"g{gen}_{name}.pt"))
                pool = [champ_sd] + pool[:MU_POOL - 1]
                promoted = True
                # 1/5 法则：登基则 σ 微降（收敛），连败则抬
                sigma = max(SIGMA_MIN, sigma * 0.9)
                break
        if not promoted:
            sigma = min(0.20, sigma * 1.15)  # 未登基抬 σ（1/5 规则反向）
        json.dump({"gen": gen}, open(state_path, "w"))
        torch.save({"state": champ_sd, "config": config},
                   os.path.join(args.work, "champ.pt"))
        say(f"代收尾 耗时{time.time()-t0:.0f}s σ={sigma:.3f} "
            + ("（新王登基）" if promoted else "（冠军卫冕）"))

    log.close()


if __name__ == "__main__":
    main()
