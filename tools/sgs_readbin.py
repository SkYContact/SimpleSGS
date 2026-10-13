# -*- coding: utf-8 -*-
"""
SGSE 二进制决策语料解析（格式契约见 src/record_agent.h 文件头）。

用法:
    from sgs_readbin import SgseReader
    r = SgseReader("smoke.bcb")
    r.vocab, r.tags, r.dims, r.games, r.decisions
批量张量:
    batches = iter_batches([paths], batch_size=512, shuffle=True)
"""
import struct

import numpy as np

MAGIC = b"SGS1"


class SgseReader:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            data = f.read()
        off = 0
        def take(n):
            nonlocal off
            b = data[off:off + n]
            if len(b) != n:
                raise EOFError(f"{path}: truncated at {off}")
            off += n
            return b
        magic = take(4)
        if magic != MAGIC:
            raise ValueError(f"{path}: bad magic {magic!r}")
        (self.version,) = struct.unpack("<I", take(4))
        def rstr():
            (n,) = struct.unpack("<H", take(2))
            return take(n).decode("utf-8")
        (vn,) = struct.unpack("<I", take(4))
        self.vocab = [rstr() for _ in range(vn)]
        (tn,) = struct.unpack("<I", take(4))
        self.tags = [rstr() for _ in range(tn)]
        self.dims = struct.unpack("<5I", take(20))  # F_SCAL SEAT_N SLOT_ATTR CAND_CAP CAND_ATTR
        self.F_SCAL, self.SEAT_N, self.SLOT_ATTR, self.CAND_CAP, self.CAND_ATTR = self.dims

        # 记录区
        self.games = []       # dict(seed=, winner=)
        self.decisions = []   # (file_offset, self, action) —— 张量惰性切片用
        self._data = data
        self._body_off = off
        rec = 0
        game = None
        while off < len(data):
            tag = data[off:off + 1]
            off += 1
            if tag == b"G":
                (seed,) = struct.unpack("<I", take(4))
                game = dict(seed=seed, winner=None, n_dec=0)
                self.games.append(game)
            elif tag == b"D":
                (who,) = struct.unpack("<i", take(4))
                rec_size = (self.F_SCAL + self.SEAT_N * self.SLOT_ATTR +
                            self.CAND_CAP * self.CAND_ATTR + self.CAND_CAP) * 4 + \
                           (self.SEAT_N + self.CAND_CAP) * 4 + 4
                take(rec_size)
                game["n_dec"] += 1
                # off 现指向记录体末尾；记录体起点 = off - rec_size（不含 'D' 与 self 字段）
                self.decisions.append((off - rec_size, who, None))
            elif tag == b"E":
                (w,) = struct.unpack("<i", take(4))
                if game:
                    game["winner"] = w
            else:
                raise ValueError(f"{path}: bad record tag {tag!r} at {off-1}")

    def rec_slice(self, idx):
        """第 idx 条决策的完整张量（float32/int32 numpy 数组）。"""
        off, who, _ = self.decisions[idx]
        d = self._data
        def f32(n):
            nonlocal off
            a = np.frombuffer(d, dtype="<f4", count=n, offset=off); off += 4 * n; return a
        def i32(n):
            nonlocal off
            a = np.frombuffer(d, dtype="<i4", count=n, offset=off); off += 4 * n; return a
        scal = f32(self.F_SCAL)
        slot_code = i32(self.SEAT_N)
        slot_attr = f32(self.SEAT_N * self.SLOT_ATTR).reshape(self.SEAT_N, self.SLOT_ATTR)
        cand_code = i32(self.CAND_CAP)
        cand_attr = f32(self.CAND_CAP * self.CAND_ATTR).reshape(self.CAND_CAP, self.CAND_ATTR)
        cand_mask = f32(self.CAND_CAP)
        (action,) = struct.unpack_from("<i", d, off)
        return dict(scal=scal, slot_code=slot_code, slot_attr=slot_attr,
                    cand_code=cand_code, cand_attr=cand_attr,
                    cand_mask=cand_mask, action=action, self_seat=who)


def iter_batches(paths, batch_size=512, shuffle=True, seed=0, val_fraction=0.02):
    """跨文件混洗产出 (batch_dict) 训练批次；返回 (train_iter, n_total)。"""
    readers = [SgseReader(p) for p in paths]
    idx = np.arange(sum(len(r.decisions) for r in readers))
    if shuffle:
        rng = np.random.default_rng(seed)
        rng.shuffle(idx)
    flat = [(r, i) for r in readers for i in range(len(r.decisions))]
    def gen(indices):
        for s in range(0, len(indices), batch_size):
            chunk = indices[s:s + batch_size]
            out = {k: [] for k in ("scal", "slot_code", "slot_attr",
                                   "cand_code", "cand_attr", "cand_mask", "action")}
            for j in chunk:
                rec = flat[j][0].rec_slice(flat[j][1])
                for k in out:
                    out[k].append(rec[k])
            yield {k: np.stack(v) for k, v in out.items()}
    n_val = int(len(idx) * val_fraction)
    # 返回可重复调用的工厂（每次调用产出新 generator），外层可多 epoch 消费
    return (lambda: gen(idx[n_val:])), (lambda: gen(idx[:n_val])), len(idx), readers


if __name__ == "__main__":
    import sys
    for p in sys.argv[1:]:
        r = SgseReader(p)
        n_win = sum(1 for g in r.games if g["winner"] is not None)
        acts = np.array([r.rec_slice(i)[1] for i in range(0)])  # 占位
        print(f"{p}: v{r.version} vocab={len(r.vocab)} games={len(r.games)} "
              f"decisions={len(r.decisions)} finished_games={n_win} "
              f"dims={r.dims}")
        # 抽一条记录做形状自检
        rec = r.rec_slice(0)
        print("  rec0:", {k: v.shape for k, v in rec.items() if hasattr(v, "shape")},
              "action=", rec["action"])
