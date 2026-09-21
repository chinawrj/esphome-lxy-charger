"""Compile the real AXP192 producer, event reducer and LCD view with fake I2C."""
import argparse, os, subprocess, tempfile
from pathlib import Path
from test_ble_serialization import STUBS
r = Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--sdk');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='lxy-board-battery-') as temp:
        root=Path(temp)
        for name,content in STUBS.items():
            if not name.startswith('esphome/core/'): continue
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content)
        p=root/'esphome/core/component.h'
        p.write_text(p.read_text()+"\nnamespace esphome { class PollingComponent : public Component { public: explicit PollingComponent(uint32_t) {} virtual void update() {} }; }\n")
        p=root/'esphome/components/i2c/i2c.h';p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text(r'''#pragma once
#include <array>
#include <vector>
#include <utility>
#include <cstdint>
namespace esphome::i2c {
class I2CDevice {
 public:
  std::array<uint8_t,256> regs{};
  std::vector<std::pair<uint8_t,uint8_t>> writes;
  int fail_read{-1}; bool fail_write{false};
  bool read_bytes(uint8_t reg,uint8_t *data,uint8_t len) {
    if (fail_read == reg) return false;
    for (uint8_t i=0;i<len;++i) data[i]=regs[reg+i];
    return true;
  }
  bool read_byte(uint8_t reg,uint8_t *data) {return read_bytes(reg,data,1);}
  bool write_byte(uint8_t reg,uint8_t data) {
    writes.emplace_back(reg,data);
    if(fail_write) return false;
    regs[reg]=data;return true;
  }
};
}
''')
        for name in ['charger_event_bus','charger_display','m5stick_battery']:
            (root/'esphome/components'/name).symlink_to(r/'components'/name,target_is_directory=True)
        out=root/'test'
        cmd=[os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-pedantic']
        if args.sdk:cmd+=['-isysroot',args.sdk]
        cmd+=['-I',str(root),str(r/'tests/battery_test.cpp'),str(r/'components/m5stick_battery/m5stick_battery.cpp'),str(r/'components/charger_event_bus/charger_event_bus.cpp'),'-o',str(out)]
        subprocess.run(cmd,check=True);subprocess.run([str(out)],check=True)
if __name__=='__main__':main()
