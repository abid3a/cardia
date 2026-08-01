#!/usr/bin/env python3
"""Train the classifier: float -> BatchNorm fold -> quantisation-aware fine-tune."""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from cardia.train import TrainConfig, train_all  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--float-epochs", type=int, default=40)
    p.add_argument("--qat-epochs", type=int, default=20)
    p.add_argument("--alpha", type=float, default=0.5, help="class-weight exponent")
    p.add_argument("--seed", type=int, default=1337)
    a = p.parse_args()

    cfg = TrainConfig(float_epochs=a.float_epochs, qat_epochs=a.qat_epochs,
                      class_weight_alpha=a.alpha, seed=a.seed)
    summary = train_all(cfgt=cfg)
    print(json.dumps({k: v for k, v in summary.items()
                      if k not in ("train_patients", "val_patients")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
