# -*- coding: utf-8 -*-
"""
SGS 1v1 策略模型 —— YGO PolicyHead4（seat attention，Perceiver 风格）的 SGS 平移版。

张量布局与 src/features.h 逐字段同源（两处文件头注释互指，改一处必须同步另一处
并重跑 sgs_export.py 对拍门）：
  scal      [B, F_SCAL=24]        标量（见 features.h 排列表）
  slot_code [B, SEAT_N=78]  i32   座位槽卡名词表 code（-1 空，embed 时 +1，pad=vocab）
  slot_attr [B, 78, SLOT_ATTR=4]  suit, rank, area, owner
  cand_code [B, CAND_CAP=48] i32  候选卡名 code（-1 = 非实体/放弃候选）
  cand_attr [B, 48, CAND_ATTR=10] 候选行（布局见 features.h CandidateRow）
  cand_mask [B, 48]               1 = 合法动作
输出:
  logits [B, 48]  候选打分（mask 外为 -inf 语义，导出图里用 -1e9 填充）
  value  [B, 1]   局面胜率估计
"""
import json
import math
import os

import torch
import torch.nn as nn

F_SCAL = 24
SEAT_N = 78
SLOT_ATTR = 4
CAND_CAP = 48
CAND_ATTR = 10

ATTR = 32      # 卡牌/槽 embedding 维（对齐 YGO emb=32）
ATTN = 48      # 注意力头通道（对齐 YGO attn=48）
DIM = 96       # 主干维度（对齐 YGO dim=96）
N_LATENT = 8   # Perceiver 潜变量查询个数
HID = 256      # 打分 MLP 隐层


class SgsPolicyNet(nn.Module):
    def __init__(self, vocab_n: int, tag_n: int = 16):
        super().__init__()
        self.vocab_n = vocab_n
        self.card_emb = nn.Embedding(vocab_n + 1, ATTR, padding_idx=vocab_n)

        self.slot_proj = nn.Linear(SLOT_ATTR, ATTR)
        self.slot_in = nn.Linear(ATTR + ATTR, DIM)
        self.slot_ln = nn.LayerNorm(DIM)
        # Perceiver 风格：可学习潜变量查询对 78 个座位 token 做交叉注意力
        self.latent = nn.Parameter(torch.zeros(N_LATENT, DIM))
        nn.init.trunc_normal_(self.latent, std=0.02)
        self.wq = nn.Linear(DIM, ATTN)
        self.wk = nn.Linear(DIM, ATTN)
        self.wv = nn.Linear(DIM, ATTN)
        self.wo = nn.Linear(ATTN, DIM)

        self.cand_proj = nn.Linear(CAND_ATTR, DIM - ATTR)
        self.scal_proj = nn.Linear(F_SCAL, DIM)
        self.score = nn.Sequential(
            nn.Linear(DIM + DIM, HID), nn.ReLU(), nn.Linear(HID, 1)
        )
        self.value = nn.Sequential(nn.Linear(DIM, HID), nn.ReLU(), nn.Linear(HID, 1))
        self.tag_n = tag_n

    def forward(self, scal, slot_code, slot_attr, cand_code, cand_attr, cand_mask):
        b = slot_code.shape[0]
        ce = self.card_emb((slot_code + 1).clamp(min=0))             # [B,78,32]
        tok = torch.cat([ce, self.slot_proj(slot_attr)], dim=-1)     # [B,78,64]
        tok = self.slot_ln(self.slot_in(tok))                        # [B,78,96]
        q = self.latent.unsqueeze(0).expand(b, -1, -1)               # [B,8,96]
        # 潜变量查询 × 座位 token（座位槽永远全在，无需 mask）
        qh, kh, vh = self.wq(q), self.wk(tok), self.wv(tok)
        att = torch.softmax(qh @ kh.transpose(1, 2) / math.sqrt(ATTN), dim=-1)
        ctx = self.wo(att @ vh)                                      # [B,8,96]
        hp = ctx.mean(dim=1) + self.scal_proj(scal)                  # [B,96]
        ce = self.card_emb((cand_code + 1).clamp(min=0))             # [B,48,32]
        cv = torch.cat([ce, self.cand_proj(cand_attr)], dim=-1)      # [B,48,96]
        pair = torch.cat([hp.unsqueeze(1).expand(-1, CAND_CAP, -1), cv], dim=-1)
        logits = self.score(pair).squeeze(-1)                        # [B,48]
        logits = logits.masked_fill(cand_mask <= 0, -1e9)
        value = self.value(hp)                                       # [B,1]
        return logits, value


# ------------------------------------------------------------------ 存取
def save_ckpt(path, model, config):
    torch.save({"state": model.state_dict(), "config": config}, path)


def load_ckpt(path, map_location="cpu"):
    ck = torch.load(path, map_location=map_location, weights_only=False)
    cfg = ck["config"]
    m = SgsPolicyNet(cfg["vocab_n"], cfg.get("tag_n", 16))
    m.load_state_dict(ck["state"])
    return m, cfg
