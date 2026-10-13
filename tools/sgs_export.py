# -*- coding: utf-8 -*-
"""
SGS 模型 ONNX 导出 + 对拍门。

用法:
    python tools/sgs_export.py --ckpt ckpt/g0.pt --out models/g0/model.onnx
对拍门：随机合成批次上 PyTorch vs onnxruntime，argmax 全行一致 + 相对差 <1e-3，
不过关直接非零退出。
"""
import argparse
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(__file__))
from sgs_model import (SgsPolicyNet, F_SCAL, SEAT_N, SLOT_ATTR,  # noqa: E402
                      CAND_CAP, CAND_ATTR, load_ckpt)


def synth_batch(vocab_n, b=8, seed=0):
    rng = np.random.default_rng(seed)
    scal = rng.normal(0, 1, (b, F_SCAL)).astype(np.float32)
    slot_code = rng.integers(-1, vocab_n, (b, SEAT_N)).astype(np.int32)
    slot_code[rng.random((b, SEAT_N)) < 0.5] = -1
    slot_attr = rng.random((b, SEAT_N, SLOT_ATTR)).astype(np.float32)
    cand_code = rng.integers(-1, vocab_n, (b, CAND_CAP)).astype(np.int32)
    cand_attr = rng.random((b, CAND_CAP, CAND_ATTR)).astype(np.float32)
    cand_mask = (rng.random((b, CAND_CAP)) < 0.3).astype(np.float32)
    cand_mask[:, 0] = 1.0  # 保证每行至少一个合法动作
    return scal, slot_code, slot_attr, cand_code, cand_attr, cand_mask


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    model, cfg = load_ckpt(args.ckpt)
    model.eval()
    vocab_n = cfg["vocab_n"]

    dummy = synth_batch(vocab_n, b=2, seed=1)
    dt = [torch.from_numpy(x) for x in dummy]
    torch_code = [torch.from_numpy(x).long() for x in (dummy[1], dummy[3])]

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    torch.onnx.export(
        model, (*dt[:1], torch_code[0], dt[2], torch_code[1], dt[4], dt[5]),
        args.out, opset_version=17, dynamo=False, do_constant_folding=True,
        input_names=["scal", "slot_code", "slot_attr", "cand_code", "cand_attr", "cand_mask"],
        output_names=["logits", "value"],
        dynamic_axes={n: {0: "B"} for n in
                      ["scal", "slot_code", "slot_attr", "cand_code", "cand_attr",
                       "cand_mask", "logits", "value"]},
    )
    print(f"[ok] 导出 {args.out}")

    # ---- 对拍门 ----
    import onnxruntime as ort
    sess = ort.InferenceSession(args.out, providers=["CPUExecutionProvider"])
    ok = True
    for s in range(3):
        b = synth_batch(vocab_n, b=16, seed=100 + s)
        feed = {"scal": b[0], "slot_code": b[1].astype(np.int64), "slot_attr": b[2],
                "cand_code": b[3].astype(np.int64), "cand_attr": b[4], "cand_mask": b[5]}
        o_logits, o_value = sess.run(["logits", "value"], feed)
        with torch.no_grad():
            t_logits, t_value = model(torch.from_numpy(b[0]),
                                      torch.from_numpy(b[1]).long(),
                                      torch.from_numpy(b[2]),
                                      torch.from_numpy(b[3]).long(),
                                      torch.from_numpy(b[4]),
                                      torch.from_numpy(b[5]))
        t_logits = t_logits.numpy()
        am_t, am_o = t_logits.argmax(-1), o_logits.argmax(-1)
        agree = (am_t == am_o).all()
        # 相对差只看合法动作位（mask 外是 -1e9 哨兵，比较无意义）
        m = b[5] > 0
        rel = np.abs(t_logits[m] - o_logits[m]) / (np.abs(t_logits[m]) + 1e-6)
        print(f"  universe{s}: argmax_agree={bool(agree)} rel_diff_max={rel.max():.2e} "
              f"value_diff={np.abs(t_value.numpy()-o_value).max():.2e}")
        ok = ok and agree and rel.max() < 1e-3
    if not ok:
        print("[x] 对拍门未通过")
        sys.exit(1)
    print("[ok] 对拍门通过（argmax 逐位一致 + rel<1e-3）")


if __name__ == "__main__":
    main()
