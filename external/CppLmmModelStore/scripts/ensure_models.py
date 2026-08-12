#!/usr/bin/env python3
import argparse
import os
import sys
from pathlib import Path


def resolve_model_home() -> Path:
    override = os.getenv("DEEPSEEK_MODEL_HOME")
    if override:
        return Path(override)
    home = os.getenv("USERPROFILE") if os.name == "nt" else os.getenv("HOME")
    if not home:
        home = os.getenv("HOME", ".")
    return Path(home) / ".models"


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Ensure DeepSeek model directories exist in the global model store."
    )
    parser.add_argument(
        "--model",
        nargs="+",
        dest="models",
        required=True,
        help="Model names to ensure. Example: --model deepseek-r1 deepseek-v3",
    )
    args = parser.parse_args()

    model_home = resolve_model_home()
    ensure_dir(model_home)

    for name in args.models:
        ensure_dir(model_home / name)

    print(str(model_home))
    return 0


if __name__ == "__main__":
    sys.exit(main())
