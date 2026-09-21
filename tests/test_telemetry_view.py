"""Compile and run the actual portable event core + LCD view, without hardware.

Run: python3 tests/test_telemetry_view.py [--sdk /path/to/MacOSX.sdk]
Optional CXX environment variable selects the native C++ compiler.
Synthetic typed telemetry validates presentation; it does not establish any
charger protocol telemetry offsets, units, or scaling.
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
    with tempfile.TemporaryDirectory(prefix="lxy-telemetry-view-") as temporary:
        executable = str(Path(temporary) / "telemetry_view_test")
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        if args.sdk:
            command += ["-isysroot", args.sdk]
        command += ["-I", str(directory.parent / "components"),
                    str(directory / "telemetry_view_test.cpp"), "-o", executable]
        subprocess.run(command, check=True)
        subprocess.run([executable], check=True)


if __name__ == "__main__":
    main()
