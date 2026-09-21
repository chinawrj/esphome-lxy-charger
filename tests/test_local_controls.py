"""Native tests for the portable controllers and real optional-module wrappers.

No GPIO, charger, display, network, or ESPHome installation is used.
Run: python3 tests/test_local_controls.py [--sdk /path/to/MacOSX.sdk]
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from test_ble_serialization import STUBS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    components = directory.parent / "components"
    with tempfile.TemporaryDirectory(prefix="lxy-local-controls-") as temporary:
        root = Path(temporary)
        for name, content in STUBS.items():
            if not name.startswith("esphome/core/"):
                continue
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        path = root / "esphome/components/output/binary_output.h"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(r'''
#pragma once
namespace esphome::output {
class BinaryOutput {
 public:
  virtual ~BinaryOutput() = default;
  virtual void set_state(bool value) { state = value; ++writes; }
  virtual void turn_on() { set_state(true); }
  virtual void turn_off() { set_state(false); }
  bool state{false};
  unsigned writes{0};
};
}
''')
        names = ("charger_event_bus", "charger_buttons", "charger_indicator")
        for name in names:
            (root / "esphome/components" / name).symlink_to(components / name, target_is_directory=True)
        executable = root / "local_controls_test"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        if args.sdk:
            command += ["-isysroot", args.sdk]
        command += ["-I", str(root), str(directory / "local_controls_test.cpp")]
        command += [str(components / name / (name + ".cpp")) for name in names]
        command += ["-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
