#!/usr/bin/env python3
"""Validate/generate/compile all optional-module combinations without hardware.

Bit 0 LCD, bit 1 Button, bit 2 LED, bit 3 Web. BLE and the event bus are
mandatory. All generated configurations use dummy example credentials.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
MODULES = (
    ("lcd", "m5stickc-plus-display.yaml"),
    ("button", "m5stickc-plus-buttons.yaml"),
    ("led", "m5stickc-plus-led.yaml"),
    ("web", "web.yaml"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("validate", "generate", "compile"), default="validate")
    parser.add_argument("--cases", default="all", help="all, or comma-separated masks 0..15")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--work-dir", type=Path, default=ROOT / ".esphome" / "matrix")
    args = parser.parse_args()
    cases = list(range(16)) if args.cases == "all" else sorted(set(int(s) for s in args.cases.split(",")))
    if not cases or any(mask < 0 or mask > 15 for mask in cases) or args.jobs < 1:
        parser.error("cases must be 0..15 and jobs must be positive")
    work = args.work_dir.resolve()
    source = work / "source"
    source.mkdir(parents=True, exist_ok=True)
    for directory in ("components", "packages", "assets"):
        shutil.copytree(ROOT / directory, source / directory, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    shutil.copy2(ROOT / "secrets.example.yaml", source / "secrets.yaml")
    logs = work / "logs"
    logs.mkdir(exist_ok=True)

    def run(mask):
        name = f"lxy-matrix-{mask:04b}"
        selected = [name for bit, (name, _) in enumerate(MODULES) if mask & (1 << bit)]
        includes = "".join(f"  {key}: !include packages/{filename}\n"
                           for bit, (key, filename) in enumerate(MODULES) if mask & (1 << bit))
        config = source / f"{name}.yaml"
        config.write_text(f'''substitutions:
  name: {name}
  friendly_name: LXY Matrix {mask:04b}
  ap_name: LXY Matrix {mask:04b}
packages:
  core: !include packages/common.yaml
{includes}
esp32:
  board: m5stick-c
  variant: ESP32
  flash_size: 4MB
  framework:
    type: esp-idf
''')
        command = [sys.executable, "-m", "esphome"]
        if args.mode == "validate":
            command += ["config", str(config)]
        else:
            command += ["compile", str(config)]
            if args.mode == "generate": command.append("--only-generate")
        env = os.environ.copy()
        env["ESPHOME_BUILD_PATH"] = str(work / "build")
        started = time.monotonic()
        with (logs / f"{name}.log").open("w") as output:
            result = subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT)
        record = {"mask": mask, "modules": selected, "mode": args.mode,
                  "status": "PASS" if result.returncode == 0 else "FAIL",
                  "seconds": round(time.monotonic() - started, 1)}
        print(json.dumps(record), flush=True)
        return record

    records = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for future in as_completed([pool.submit(run, mask) for mask in cases]):
            records.append(future.result())
            (work / f"results-{args.mode}.json").write_text(json.dumps(sorted(records, key=lambda r: r["mask"]), indent=2) + "\n")
    print(f"{sum(r['status'] == 'PASS' for r in records)}/{len(records)} combinations passed")
    return int(any(r["status"] != "PASS" for r in records))


if __name__ == "__main__":
    raise SystemExit(main())
