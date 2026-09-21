"""Run the real web adapter + event bus against observable native entity shells.

No network, ESPHome install, charger, or telemetry decoder is involved.
Run: python3 tests/test_web_telemetry.py [--sdk /path/to/MacOSX.sdk]
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


# Dedicated test shells. Production event reduction, subscriptions, time-based
# invalidation and entity publication all run from the real .cpp files.
STUBS = {
    "esphome/core/component.h": r'''
#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t fake_millis = 0;
inline uint32_t millis() { return fake_millis; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  void mark_failed() { failed_ = true; }
  bool is_failed() const { return failed_; }
 private:
  bool failed_{false};
};
}
''',
    "esphome/core/hal.h": '#pragma once\n#include "component.h"\n',
    "esphome/core/automation.h": r'''
#pragma once
namespace esphome {
template<class... T> class Trigger { public: void trigger(T...) {} };
}
''',
    "esphome/core/log.h": r'''
#pragma once
inline void native_log(const char *, const char *, ...) {}
#define ESP_LOGE(...) native_log(__VA_ARGS__)
#define ESP_LOGW(...) native_log(__VA_ARGS__)
#define ESP_LOGI(...) native_log(__VA_ARGS__)
#define ESP_LOGD(...) native_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) native_log(__VA_ARGS__)
''',
    "native_entity.h": r'''
#pragma once
#include <cmath>
#include <string>
#include <vector>
namespace native {
template<typename T> class Entity {
 public:
  explicit Entity(T initial) : state(initial) {}
  void publish_state(T value) { state = value; states.push_back(value); }
  T state;
  std::vector<T> states;
};
}
''',
    "esphome/components/sensor/sensor.h": r'''
#pragma once
#include "native_entity.h"
namespace esphome::sensor {
class Sensor : public native::Entity<float> { public: Sensor() : Entity(NAN) {} };
}
''',
    "esphome/components/binary_sensor/binary_sensor.h": r'''
#pragma once
#include "native_entity.h"
namespace esphome::binary_sensor {
class BinarySensor : public native::Entity<bool> { public: BinarySensor() : Entity(false) {} };
}
''',
    "esphome/components/text_sensor/text_sensor.h": r'''
#pragma once
#include "native_entity.h"
namespace esphome::text_sensor {
class TextSensor : public native::Entity<std::string> { public: TextSensor() : Entity("") {} };
}
''',
    "esphome/components/number/number.h": r'''
#pragma once
#include "native_entity.h"
namespace esphome::number {
class Number : public native::Entity<float> {
 public:
  Number() : Entity(NAN) {}
  virtual ~Number() = default;
 protected:
  virtual void control(float) = 0;
};
}
''',
    "esphome/components/button/button.h": r'''
#pragma once
namespace esphome::button {
class Button { public: virtual ~Button() = default; protected: virtual void press_action() = 0; };
}
''',
    "esphome/components/switch/switch.h": r'''
#pragma once
#include "native_entity.h"
namespace esphome::switch_ {
class Switch : public native::Entity<bool> {
 public:
  Switch() : Entity(false) {}
  virtual ~Switch() = default;
 protected:
  virtual void write_state(bool) = 0;
};
}
''',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", help="Explicit matching macOS SDK when needed")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    components = directory.parent / "components"
    with tempfile.TemporaryDirectory(prefix="lxy-web-telemetry-") as temporary:
        root = Path(temporary)
        for name, content in STUBS.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        for name in ("charger_event_bus", "charger_web"):
            (root / "esphome/components" / name).symlink_to(components / name, target_is_directory=True)
        executable = root / "web_telemetry_test"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   "-Wno-missing-field-initializers", "-pedantic"]
        if args.sdk:
            command += ["-isysroot", args.sdk]
        command += ["-I", str(root), str(directory / "web_telemetry_test.cpp"),
                    str(components / "charger_web/charger_web.cpp"),
                    str(components / "charger_event_bus/charger_event_bus.cpp"), "-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
