#!/usr/bin/env python3
"""Export the trained QAT model to C, and verify the integer path against it.

Three implementations must agree for the project's central claim to hold:
the QAT float model, the numpy integer reference, and the C firmware. This
script produces the C, then checks numpy-int vs QAT-float here; the C side is
checked by sim/parity.
"""
import json
import sys
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from cardia import codegen, dataset, int_reference, model as M, quantize as Q  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    ck = torch.load(ROOT / "ml" / "artifacts" / "cardia.pt", weights_only=True)
    folded = M.CardiaNetFolded()
    folded.load_state_dict(ck["folded"])
    qat = Q.CardiaNetQAT(folded)
    qat.load_state_dict(ck["qat"])
    qat.eval()

    qm = Q.export(qat)
    info = codegen.generate(qm, ROOT / "firmware" / "src" / "nn")
    print(json.dumps(info, indent=2))

    # --- numpy integer reference vs QAT float, on held-out DS2 beats -------
    ds2 = dataset.load_cached("ds2")
    n = min(4000, len(ds2))
    rng = np.random.default_rng(0)
    idx = rng.choice(len(ds2), size=n, replace=False)
    beats, rrs = ds2.beats[idx], ds2.rr[idx]

    intnet = int_reference.IntCardiaNet(qm)
    int_pred = intnet.predict_batch(beats, rrs)
    with torch.no_grad():
        qat_pred = qat(torch.from_numpy(beats), torch.from_numpy(rrs)).argmax(1).numpy()

    agree = float((int_pred == qat_pred).mean())
    print(f"\ninteger reference vs QAT float: {agree*100:.3f}% agreement on {n} beats")
    (ROOT / "ml" / "artifacts" / "export_summary.json").write_text(json.dumps(
        {**info, "int_vs_qat_agreement": agree, "n_compared": n}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
