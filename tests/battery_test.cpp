#include "esphome/components/m5stick_battery/m5stick_battery.h"
#include "esphome/components/charger_display/charger_display.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace esphome;
using namespace charger_event_bus;
struct Fixture {
  ChargerEventBus bus; m5stick_battery::M5StickBattery battery;
  Fixture() {
    fake_millis=1000;battery.set_event_bus(&bus);
    battery.regs[3]=3;battery.regs[0x82]=0x83;battery.regs[1]=0x20;
    raw(0x78,3750,4);raw(0x7a,200,5);raw(0x7c,0,5);
  }
  void raw(uint8_t reg,uint16_t value,uint8_t bits) {
    battery.regs[reg]=value>>bits;
    // Reserved high bits in the low register must be masked out.
    battery.regs[reg+1]=(value&((1<<bits)-1))|uint8_t(0xff<<bits);
  }
  void drain(){for(int i=0;i<8;++i)bus.loop();}
  void update(){battery.update();drain();}
  std::string view(uint32_t now=1000) {
    auto v=charger_display::make_view(bus.snapshot(),now);std::string s;
    for(size_t i=0;i<v.count;++i) { s+=v.labels[i].text+"|"; }
    return s;
  }
};
int main(){
  Fixture f;f.battery.setup();f.drain();
  assert(f.battery.writes.size()==1 && f.battery.writes[0].first==0x82 && f.battery.writes[0].second==0xc3);
  assert(!f.bus.snapshot().battery_valid); // No stale pre-enable ADC sample.
  f.update();auto s=f.bus.snapshot();
  assert(s.battery_fresh(1000) && s.battery_present && std::fabs(s.battery_voltage-4.125f)<0.0001f);
  assert(s.battery_current_ma==100 && f.view().find("充+100.0mA")!=std::string::npos);
  assert(!s.connected && !s.telemetry_valid && std::isnan(s.output_voltage));
  f.raw(0x7a,0,5);f.raw(0x7c,271,5);f.update();
  assert(f.bus.snapshot().battery_current_ma==-135.5f && f.view().find("放-135.5mA")!=std::string::npos);
  Event disconnected;disconnected.type=EventType::CONNECTION;assert(f.bus.publish(disconnected));f.drain();
  assert(f.bus.snapshot().battery_current_ma==-135.5f); // Independent of BLE.
  assert(f.view(7000).find("-- V / -- mA")!=std::string::npos);
  assert(f.view(6999).find("放-135.5mA")!=std::string::npos);
  f.raw(0x7c,0,5);f.update();assert(f.view().find("0.0mA")!=std::string::npos);
  f.battery.fail_read=0x78;f.update();assert(!f.bus.snapshot().battery_valid && std::isnan(f.bus.snapshot().battery_current_ma));
  f.battery.fail_read=-1;f.update();assert(f.bus.snapshot().battery_valid);
  f.battery.regs[1]=0;f.update();assert(f.bus.snapshot().battery_valid && !f.bus.snapshot().battery_present);
  assert(f.view().find("未检测到")!=std::string::npos);
  f.battery.regs[3]=0;f.update();assert(!f.bus.snapshot().battery_valid && f.battery.writes.size()==1);
  Fixture failed;failed.battery.fail_write=true;failed.update();assert(!failed.bus.snapshot().battery_valid);
  failed.battery.fail_write=false;failed.update();failed.update();assert(failed.bus.snapshot().battery_valid);
  for(auto reg : {3,0x82,1,0x78}) {
    Fixture fault;fault.battery.regs[0x82]=0xc3;fault.battery.fail_read=reg;fault.update();
    assert(!fault.bus.snapshot().battery_valid);
  }
  Event invalid;invalid.type=EventType::BOARD_BATTERY;invalid.battery_valid=invalid.battery_present=true;
  invalid.battery_voltage=4;invalid.battery_charge_ma=-1;invalid.battery_discharge_ma=0;
  assert(f.bus.publish(invalid));f.drain();assert(!f.bus.snapshot().battery_valid);
  Event sample;sample.type=EventType::BOARD_BATTERY;sample.battery_valid=sample.battery_present=true;
  sample.battery_voltage=4;sample.battery_charge_ma=1;sample.battery_discharge_ma=0;sample.sampled_at=UINT32_MAX-100;
  assert(f.bus.publish(sample));f.drain();assert(f.bus.snapshot().battery_fresh(200));
  assert(!f.bus.snapshot().battery_fresh(uint32_t(sample.sampled_at+6000)));
  Event meter;meter.type=EventType::UI_STATE;meter.ui_mode=UiMode::METER;assert(f.bus.publish(meter));f.drain();
  assert(f.view().find("本机电池")==std::string::npos);
  puts("PASS: AXP192 12/13-bit ADC scaling/sign, enable preservation, failure/recovery, battery presence, stale/wrap, BLE and meter isolation");
}
