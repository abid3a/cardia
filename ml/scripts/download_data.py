#!/usr/bin/env python3
"""Fetch the MIT-BIH records this project uses and build the DS1/DS2 caches."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from cardia import dataset, splits  # noqa: E402


def main() -> int:
    print(f"downloading {len(splits.ALL_RECORDS)} records "
          f"(DS1={len(splits.DS1)}, DS2={len(splits.DS2)}, "
          f"paced excluded={list(splits.PACED_RECORDS)})")
    dataset.download()
    summary = dataset.build_and_cache()
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
