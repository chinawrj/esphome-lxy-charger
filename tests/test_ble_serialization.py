"""Run the real BLE component against a fake clock, GATT transport and ESPHome shell.

No ESPHome installation, radio, network or device is used. The real event bus,
frame parser and BLE transaction implementation are compiled, not reimplemented.
Run: python3 tests/test_ble_serialization.py [--sdk /path/to/MacOSX.sdk]
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


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
  void status_set_warning() {}
  void status_clear_warning() {}
 private:
  bool failed_{false};
};
}
''',
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
    "esp_gattc_api.h": r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
using esp_gatt_if_t = uint8_t;
enum esp_gattc_cb_event_t {
  ESP_GATTC_OPEN_EVT, ESP_GATTC_DISCONNECT_EVT, ESP_GATTC_CLOSE_EVT,
  ESP_GATTC_SEARCH_CMPL_EVT, ESP_GATTC_REG_FOR_NOTIFY_EVT,
  ESP_GATTC_WRITE_DESCR_EVT, ESP_GATTC_WRITE_CHAR_EVT, ESP_GATTC_NOTIFY_EVT
};
struct esp_ble_gattc_cb_param_t {
  struct { int status{}; } open, search_cmpl;
  struct { uint16_t handle{}; int status{}; } reg_for_notify;
  struct { uint16_t handle{}, conn_id{}; int status{}; } write;
  struct { uint16_t handle{}, conn_id{}, value_len{}; uint8_t *value{}; } notify;
};
constexpr int ESP_OK = 0, ESP_GATT_OK = 0;
constexpr int ESP_GATT_WRITE_TYPE_NO_RSP = 1, ESP_GATT_AUTH_REQ_NONE = 0;
constexpr uint8_t ESP_GATT_CHAR_PROP_BIT_WRITE_NR = 4, ESP_GATT_CHAR_PROP_BIT_NOTIFY = 16;
inline std::vector<std::vector<uint8_t>> native_writes;
inline int native_write_result = ESP_OK;
inline int esp_ble_gattc_write_char(esp_gatt_if_t, uint16_t, uint16_t, size_t size,
                                   uint8_t *data, int, int) {
  native_writes.emplace_back(data, data + size);
  return native_write_result;
}
''',
    "esphome/components/ble_client/ble_client.h": r'''
#pragma once
#include "esp_gattc_api.h"
namespace esphome {
namespace esp32_ble_tracker { enum class ClientState { IDLE, ESTABLISHED }; }
namespace ble_client {
struct Descriptor { uint16_t handle{3}; };
struct Characteristic {
  uint16_t handle{1};
  uint8_t properties{ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_NOTIFY};
  Descriptor descriptor;
  Descriptor *get_descriptor(uint16_t) { return &descriptor; }
};
class BLEClient {
 public:
  esp32_ble_tracker::ClientState current{esp32_ble_tracker::ClientState::ESTABLISHED};
  bool enabled{true};
  unsigned disconnects{0};
  const char *address_str() const { return "test"; }
  esp32_ble_tracker::ClientState state() const { return current; }
  void set_enabled(bool value) { enabled = value; if (!value) disconnect(); }
  void disconnect() { ++disconnects; current = esp32_ble_tracker::ClientState::IDLE; }
  esp_gatt_if_t get_gattc_if() const { return 1; }
  uint16_t get_conn_id() const { return 1; }
  int register_for_notify(uint16_t) { return ESP_OK; }
  Characteristic *get_characteristic(uint16_t, uint16_t) { return &characteristic_; }
 private:
  Characteristic characteristic_;
};
class BLEClientNode {
 public:
  virtual ~BLEClientNode() = default;
  virtual void loop() {}
  virtual void gattc_event_handler(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *) {}
  BLEClient *parent() { return parent_; }
 protected:
  BLEClient *parent_{nullptr};
  esp32_ble_tracker::ClientState node_state{esp32_ble_tracker::ClientState::IDLE};
};
}
}
''',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", help="Explicit matching macOS SDK when needed")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    components = directory.parent / "components"
    with tempfile.TemporaryDirectory(prefix="lxy-ble-serialization-") as temporary:
        root = Path(temporary)
        for name, content in STUBS.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        # The component includes these through ESPHome's generated source tree.
        for component in ("lxy_charger", "charger_event_bus"):
            (root / "esphome/components" / component).symlink_to(components / component, target_is_directory=True)
        executable = root / "ble_serialization_test"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        if args.sdk:
            command += ["-isysroot", args.sdk]
        command += ["-I", str(root), str(directory / "ble_serialization_test.cpp"),
                    str(components / "lxy_charger/lxy_charger.cpp"),
                    str(components / "charger_event_bus/charger_event_bus.cpp"), "-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
