#pragma once

#include "../charger_event_bus/event_core.h"
#include <array>
#include <cstdio>
#include <string>

namespace esphome::charger_display {
// Shared by the hardware renderer and documented previews. Application input
// comes only from typed events, never from another optional module's objects.
enum class Font { SMALL, MEDIUM, LARGE, HERO, POWER };
enum class Ink { WHITE, MUTED, GREEN, AMBER };
struct Label {
  int x{0}, y{0};
  Font font{Font::SMALL};
  Ink ink{Ink::WHITE};
  bool right{false};
  std::string text;
};
struct View {
  std::array<Label, 16> labels{};
  size_t count{0};
  void add(int x, int y, Font font, Ink ink, const std::string &text, bool right = false) {
    if (count < labels.size()) labels[count++] = {x, y, font, ink, right, text};
  }
};
inline std::string number(float value) {
  if (!std::isfinite(value)) return "--.-";
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}
inline const char *hold_text(charger_event_bus::UiHold hold) {
  using charger_event_bus::UiHold;
  switch (hold) {
    case UiHold::HELP: return "松开 B 查看帮助";
    case UiHold::EDIT: return "松开 A 进入编辑";
    case UiHold::REVIEW: return "松开 A 查看确认页";
    case UiHold::APPLY: return "松开 A 提交一次";
    case UiHold::CANCEL: return "松开 B 取消，不发送";
    case UiHold::RELEASE: return "按住过久，请松开重试";
    default: return "";
  }
}
inline const char *notice_text(charger_event_bus::UiNotice notice) {
  using charger_event_bus::UiNotice;
  switch (notice) {
    case UiNotice::CONNECTED: return "连接成功";
    case UiNotice::CONNECT_FAILED: return "连接失败，请重试";
    case UiNotice::APPLIED: return "设置已确认";
    case UiNotice::REFRESHED: return "参数已刷新";
    case UiNotice::CANCELLED: return "已取消，未发送";
    case UiNotice::UNCHANGED: return "设置未改变，未发送";
    case UiNotice::LIMIT: return "已达验证范围边界";
    case UiNotice::DISPLAY_UNAVAILABLE: return "显示不可用，已取消";
    case UiNotice::NOT_READY: return "设备尚未就绪，请稍候";
    case UiNotice::BUSY: return "正在处理，请稍候";
    case UiNotice::CONFIG_CHANGED: return "设备设定已变化，已取消";
    case UiNotice::TIMED_OUT: return "编辑超时，未发送";
    case UiNotice::INPUT_INTERRUPTED: return "按键操作中断，已取消";
    case UiNotice::QUEUE_FULL: return "操作未提交，请重试";
    case UiNotice::REJECTED: return "操作被拒绝，未发送";
    case UiNotice::FAILED: return "操作失败，请检查设备";
    case UiNotice::UNKNOWN: return "结果未确认，未重发";
    default: return "";
  }
}
inline Ink notice_ink(charger_event_bus::UiNotice notice) {
  using charger_event_bus::UiNotice;
  switch (notice) {
    case UiNotice::APPLIED: case UiNotice::REFRESHED: case UiNotice::CONNECTED: return Ink::GREEN;
    case UiNotice::CANCELLED: case UiNotice::UNCHANGED: case UiNotice::TIMED_OUT: return Ink::MUTED;
    default: return Ink::AMBER;
  }
}
inline const char *output_badge(charger_event_bus::OutputState state) {
  using charger_event_bus::OutputState;
  switch (state) {
    case OutputState::LIVE: return "实时";
    case OutputState::UNSUPPORTED: return "未解码";
    case OutputState::STALE: return "已过期";
    case OutputState::INVALID: return "数据无效";
    case OutputState::INITIALIZING: return "初始化";
    default: return "等待数据";
  }
}
inline const char *output_reason(const charger_event_bus::Snapshot &state, uint32_t now) {
  using charger_event_bus::OutputState;
  switch (state.output_state(now)) {
    case OutputState::LIVE: return state.telemetry_inferred ? "电压映射待核验，电流暂缓" : "实时输出，与下方设定值独立";
    case OutputState::UNSUPPORTED: return "通信正常，输出数据尚未解码";
    case OutputState::STALE: return "输出数据已过期，等待更新";
    case OutputState::INVALID: return "输出数据无效，等待更新";
    case OutputState::INITIALIZING: return "连接成功，正在读取设备";
    case OutputState::WAITING: return "等待首个实时输出数据";
    default: return state.connection_enabled ? "正在连接充电器" : "蓝牙已关闭，可手动连接";
  }
}
inline View make_view(const charger_event_bus::Snapshot &state, uint32_t now) {
  using namespace charger_event_bus;
  View view;
  if (state.ui_mode == UiMode::METER) {
    const bool fresh = state.telemetry_fresh(now);
    const float voltage = fresh && (state.telemetry_channels & 1) ? state.output_voltage : NAN;
    const float current = fresh && (state.telemetry_channels & 2) ? state.output_current : NAN;
    // Both measurements must belong to the same accepted, fresh telemetry event.
    // Setpoints never participate, and an unavailable current never becomes zero watts.
    const float power = std::isfinite(voltage) && std::isfinite(current) ? voltage * current : NAN;
    view.add(192, -9, Font::HERO, std::isfinite(voltage) ? Ink::WHITE : Ink::MUTED, number(voltage), true);
    view.add(232, 16, Font::POWER, state.telemetry_inferred ? Ink::AMBER : Ink::MUTED,
        state.telemetry_inferred ? "V*" : "V", true);
    view.add(192, 41, Font::HERO, std::isfinite(current) ? Ink::WHITE : Ink::MUTED, number(current), true);
    view.add(232, 66, Font::POWER, Ink::MUTED, "A", true);
    view.add(192, 99, Font::POWER, std::isfinite(power) ? Ink::WHITE : Ink::MUTED, number(power), true);
    view.add(232, 99, Font::POWER, Ink::MUTED, "W", true);
    return view;
  }
  const char *link = state.connected ? "BLE 已连接" :
      (state.connection_enabled ? "BLE 连接中" : "BLE 已断开");
  const Ink link_ink = state.connected ? Ink::GREEN : Ink::AMBER;
  view.add(8, 1, Font::SMALL, link_ink, link);
  if (state.ui_mode == UiMode::HELP) {
    view.add(232, 1, Font::SMALL, Ink::WHITE, "操作帮助", true);
    view.add(8, 24, Font::SMALL, Ink::MUTED, "红色是固定灯色，不表示故障");
    view.add(8, 44, Font::SMALL, Ink::WHITE, "常亮 已连接    慢闪 连接中");
    view.add(8, 62, Font::SMALL, Ink::WHITE, "熄灭 已断开");
    view.add(8, 83, Font::SMALL, Ink::WHITE, "A 选择    长 A 编辑");
    view.add(8, 101, Font::SMALL, Ink::WHITE, "B 刷新/连接    长 B 帮助");
    view.add(8, 118, Font::SMALL, Ink::GREEN, "短按 A 或 B 返回实时页面");
    return view;
  }
  const bool editing = state.ui_mode == UiMode::EDIT || state.ui_mode == UiMode::CONFIRM ||
      state.ui_mode == UiMode::SUBMITTING;
  if (editing) {
    const bool adjust = state.ui_mode == UiMode::EDIT;
    const bool confirm = state.ui_mode == UiMode::CONFIRM;
    view.add(232, 1, Font::SMALL, Ink::AMBER, adjust ? "草稿未发送" :
        (confirm ? "确认设置" : "等待验证"), true);
    view.add(8, 24, Font::LARGE, state.ui_field == UiField::VOLTAGE ? Ink::AMBER : Ink::WHITE,
        number(state.ui_voltage));
    view.add(132, 24, Font::LARGE, state.ui_field == UiField::CURRENT ? Ink::AMBER : Ink::WHITE,
        number(state.ui_current));
    view.add(8, 69, Font::SMALL, Ink::MUTED, "设定电压 / V");
    view.add(132, 69, Font::SMALL, Ink::MUTED, "设定电流 / A");
    const char *hold = hold_text(state.ui_hold);
    if (adjust) {
      view.add(8, 88, Font::SMALL, state.ui_notice == UiNotice::LIMIT ? Ink::AMBER : Ink::MUTED,
          state.ui_notice == UiNotice::LIMIT ? notice_text(state.ui_notice) : "仅修改草稿，不会自动发送");
      view.add(8, 104, Font::SMALL, Ink::WHITE, "A -0.1     B +0.1");
      view.add(8, 118, Font::SMALL, *hold ? Ink::AMBER : Ink::MUTED,
          *hold ? hold : "长 A 确认    长 B 取消");
    } else if (confirm) {
      view.add(8, 88, Font::SMALL, Ink::MUTED,
          "原设定 " + number(state.voltage) + " V / " + number(state.current) + " A");
      view.add(8, 104, Font::SMALL, Ink::AMBER, "再次长按 A 才会提交");
      view.add(8, 118, Font::SMALL, *hold ? Ink::AMBER : Ink::MUTED,
          *hold ? hold : "长 A 提交一次    B 取消");
    } else {
      view.add(8, 91, Font::SMALL, Ink::AMBER, "已提交，等待设备回读确认");
      view.add(8, 116, Font::SMALL, Ink::MUTED, "不自动重发，请稍候");
    }
    return view;
  }
  const OutputState output = state.output_state(now);
  const bool fresh = output == OutputState::LIVE;
  view.add(232, 1, Font::SMALL, fresh && !state.telemetry_inferred ? Ink::GREEN : Ink::AMBER,
      fresh && state.telemetry_inferred ? "电压待核" : output_badge(output), true);
  view.add(8, 23, Font::LARGE, fresh ? Ink::WHITE : Ink::MUTED,
      fresh ? number(state.output_voltage) : "--.-");
  view.add(132, 23, Font::LARGE, fresh && std::isfinite(state.output_current) ? Ink::WHITE : Ink::MUTED,
      fresh ? number(state.output_current) : "--.-");
  view.add(8, 69, Font::SMALL, Ink::MUTED, "实时电压 / V");
  view.add(132, 69, Font::SMALL, Ink::MUTED, (state.telemetry_supported && !(state.telemetry_channels & 2)) ? "电流暂缓 / A" : "实时电流 / A");
  const bool failed = state.ui_notice == UiNotice::FAILED || state.ui_notice == UiNotice::UNKNOWN ||
      state.ui_notice == UiNotice::CONNECT_FAILED;
  const char *notice = notice_text(state.ui_notice);
  const bool show_notice = *notice && uint32_t(now - state.ui_updated_at) < (failed ? 6000u : 3000u);
  view.add(8, 86, Font::SMALL, show_notice ? notice_ink(state.ui_notice) : Ink::MUTED,
      state.ui_mode == UiMode::CONNECTING ? "正在连接设备，请稍候" :
      state.ui_mode == UiMode::REFRESHING ? "正在读取设定值，请稍候" :
      state.busy ? "正在处理请求，请稍候" :
      (show_notice ? notice : output_reason(state, now)));
  const std::string v = state.ready ? number(state.voltage) : "--.-";
  const std::string a = state.ready ? number(state.current) : "--.-";
  view.add(8, 104, Font::SMALL, state.ui_buttons_ready && state.ui_field == UiField::VOLTAGE ? Ink::AMBER : Ink::MUTED,
      "设定 " + v + " V");
  view.add(132, 104, Font::SMALL, state.ui_buttons_ready && state.ui_field == UiField::CURRENT ? Ink::AMBER : Ink::MUTED,
      "设定 " + a + " A");
  const char *hold = hold_text(state.ui_hold);
  view.add(8, 118, Font::SMALL, *hold ? Ink::AMBER : Ink::MUTED,
      *hold ? hold : (state.ui_buttons_ready ? (state.ready ? "A选 长A编辑 B刷新 长B帮助" :
       (!state.connected && !state.connection_enabled ? "B 连接    长 B 帮助" : "连接处理中    长 B 帮助")) :
       "红灯仅表示连接状态"));
  return view;
}
}  // namespace esphome::charger_display
