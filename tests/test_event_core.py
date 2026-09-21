"""Compile and run portable event-bus tests, without ESPHome or hardware.

Run: python3 test_event_core.py [--sdk /path/to/MacOSX.sdk]
Optional CXX environment variable selects the native C++ compiler.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", help="Explicit matching macOS SDK when needed")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="lxy-event-core-") as temporary:
        executable = str(Path(temporary) / "event_core_test")
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        if args.sdk:
            command += ["-isysroot", args.sdk]
        include = directory.parent / "components" / "charger_event_bus"
        command += ["-I", str(include), str(directory / "event_core_test.cpp"), "-o", executable]
        subprocess.run(command, check=True)
        subprocess.run([executable], check=True)


if __name__ == "__main__":
    main()
