# -*- coding: utf-8 -*-
"""
SGS BC 训练（蒸馏 AiAgent 语料产 g0 种子权重，也用于后续任何 SGSE 语料）。

用法:
    python tools/sgs_train.py --data smoke.bcb --out ckpt/g0.pt [--epochs 6] [--lr 1e-3]
    python tools/sgs_train.py --data a.bcb b.bcb --init ckpt/g0.pt ...   # 继续训
"""
import argparse
import os
import sys
import time

import numpy as np
import torch
import torch.nn.functional as F

sys.path.insert(0, os.path.dirname(__file__))
from sgs_model import SgsPolicyNet, save_ckpt  # noqa: E402
from sgs_readbin import iter_batches  # noqa: E402


def to_torch(b, dev):
    return (torch.from_numpy(b["scal"]).to(dev),
            torch.from_numpy(b["slot_code"]).long().to(dev),
            torch.from_numpy(b["slot_attr"]).to(dev),
            torch.from_numpy(b["cand_code"]).long().to(dev),
            torch.from_numpy(b["cand_attr"]).to(dev),
            torch.from_numpy(b["cand_mask"]).to(dev),
            torch.from_numpy(b["action"]).long().to(dev))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", nargs="+", required=True)
    ap.add_argument("--out", default="ckpt/g0.pt")
    ap.add_argument("--init", default=None, help="从已有 ckpt 继续训")
    ap.add_argument("--epochs", type=int, default=6)
    ap.add_argument("--batch", type=int, default=512)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--dev", default="cpu")
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    dev = args.dev
    train_gen, val_gen, n_total, readers = iter_batches(args.data, args.batch, True, args.seed)
    vocab_n = len(readers[0].vocab)
    print(f"语料 {n_total} 决策 / {sum(len(r.games) for r in readers)} 局, vocab={vocab_n}, dev={dev}")

    if args.init:
        ck = torch.load(args.init, map_location="cpu", weights_only=False)
        model = SgsPolicyNet(ck["config"]["vocab_n"]).to(dev)
        model.load_state_dict(ck["state"])
    else:
        model = SgsPolicyNet(vocab_n).to(dev)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)

    for ep in range(args.epochs):
        model.train()
        t0, seen, tot_loss, tot_hit = time.time(), 0, 0.0, 0
        for b in train_gen():
            *inp, act = to_torch(b, dev)
            logits, value = model(*inp)
            loss = F.cross_entropy(logits, act)
            opt.zero_grad()
            loss.backward()
            opt.step()
            tot_loss += loss.item() * len(act)
            tot_hit += (logits.argmax(-1) == act).sum().item()
            seen += len(act)
        # val
        model.eval()
        v_hit, v_seen = 0, 0
        with torch.no_grad():
            for b in val_gen():
                *inp, act = to_torch(b, dev)
                logits, _ = model(*inp)
                v_hit += (logits.argmax(-1) == act).sum().item()
                v_seen += len(act)
        print(f"ep{ep}: loss={tot_loss/max(seen,1):.4f} train_acc={tot_hit/max(seen,1):.3f} "
              f"val_acc={v_hit/max(v_seen,1):.3f} ({time.time()-t0:.1f}s, {seen} samples)")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    save_ckpt(args.out, model, {"vocab_n": vocab_n, "tag_n": len(readers[0].tags),
                                "epochs": args.epochs, "data": args.data})
    print(f"[ok] {args.out}")


if __name__ == "__main__":
    main()
