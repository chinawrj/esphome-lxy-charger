#pragma once
#include "esphome/components/charger_event_bus/event_core.h"
#include <functional>
#include <utility>

namespace esphome::charger_buttons {
using namespace charger_event_bus;

// Portable state machine: no GPIO, BLE, LCD, web or ESPHome dependencies.
class ButtonsController {
 public:
  using Request = std::function<uint32_t(EventType, float, float)>;
  void set_requester(Request request) { request_ = std::move(request); }
  void edge(bool button_a, bool down, uint32_t sequence, uint32_t now, const Snapshot &state) {
    if (sequence_seen_ && sequence != last_sequence_ + 1) {
      held_a_ = held_b_ = chord_ = false;
      cancel_("Input interrupted; edit cancelled", Result::REJECTED);
      last_sequence_ = sequence;
      return;  // A missing edge must never become an accidental long press.
    }
    sequence_seen_ = true;
    last_sequence_ = sequence;
    tick(now, state);
    bool &held = button_a ? held_a_ : held_b_;
    uint32_t &started = button_a ? started_a_ : started_b_;
    if (down) {
      if (held) { chord_ = true; return; }
      held = true;
      started = now;
      if (held_a_ && held_b_) {
        chord_ = true;
        if (mode_ == UiMode::EDIT || mode_ == UiMode::CONFIRM)
          cancel_("Two buttons pressed; edit cancelled", Result::REJECTED);
      }
      return;
    }
    if (!held) return;
    held = false;
    const uint32_t duration = now - started;
    if (chord_) {
      if (!held_a_ && !held_b_) chord_ = false;
      return;
    }
    if (duration < 40 || duration > 5000) return;
    gesture_(button_a, duration >= 800, now, state);
  }

  void tick(uint32_t now, const Snapshot &state) {
    if (mode_ != UiMode::EDIT && mode_ != UiMode::CONFIRM) return;
    if (!display_alive_(state, now)) cancel_("Display unavailable; edit cancelled", Result::REJECTED);
    else if (!state.ready) cancel_("Charger disconnected; edit cancelled", Result::REJECTED);
    else if (state.busy) cancel_("Another operation started; edit cancelled", Result::REJECTED);
    else if (!valid_(state.voltage, true) || !valid_(state.current, false) ||
             tenths_(state.voltage) != baseline_v_ || tenths_(state.current) != baseline_a_)
      cancel_("Readback changed; edit cancelled", Result::REJECTED);
    else if (now - last_action_ >= 30000) cancel_("Edit timed out; nothing sent", Result::INFO);
  }

  void observe(const Event &event, const Snapshot &state, uint32_t now) {
    if (event.type == EventType::UI_DISPLAY) {
      display_seen_ = event.ui_display_ready;
      display_seen_at_ = now;
    }
    if (event.type == EventType::STATUS && pending_id_ && event.request_id == pending_id_ &&
        event.result != Result::INFO && event.result != Result::ACCEPTED) {
      pending_id_ = 0;
      mode_ = UiMode::VIEW;
      result_ = event.result;
      message_ = event.message;
      changed_ = true;
    }
    tick(now, state);
  }

  Event ui_event() const {
    Event event{}; event.type = EventType::UI_STATE;
    event.source = "buttons";
    event.ui_mode = mode_;
    event.ui_field = field_;
    event.voltage = draft_v_ / 10.0f;
    event.current = draft_a_ / 10.0f;
    event.request_id = last_request_id_;
    event.result = result_;
    event.message = message_;
    return event;
  }
  bool changed() const { return changed_; }
  void published() { changed_ = false; }
  void display_publish_failed() {
    // Never allow unseen confirmation state to authorize a later Apply.
    if (mode_ == UiMode::EDIT || mode_ == UiMode::CONFIRM)
      cancel_("Display update unavailable; edit cancelled", Result::REJECTED);
  }

 private:
  static int tenths_(float value) { return static_cast<int>(std::round(value * 10.0f)); }
  bool display_alive_(const Snapshot &state, uint32_t now) const {
    return display_seen_ && state.ui_display_ready && now - display_seen_at_ < 3000;
  }
  static bool valid_(float value, bool voltage) {
    if (!std::isfinite(value)) return false;
    const float scaled = value * 10.0f;
    const float rounded = std::round(scaled);
    return std::fabs(scaled - rounded) < 0.001f &&
        (voltage ? rounded >= 582 && rounded <= 584 : rounded >= 49 && rounded <= 51);
  }
  void feedback_(const char *message, Result result = Result::INFO) {
    message_ = message; result_ = result; changed_ = true;
  }
  void cancel_(const char *message, Result result) {
    if (pending_id_) return;  // An already-submitted request is never replayed/cancelled here.
    if (held_a_ || held_b_) chord_ = true;  // Cancel the current hold as well as its draft.
    mode_ = UiMode::VIEW;
    last_request_id_ = 0;
    feedback_(message, result);
  }
  void submit_(bool apply) {
    const uint32_t id = request_ ? request_(apply ? EventType::REQUEST_APPLY_CONFIG : EventType::REQUEST_READ_CONFIG,
                                          draft_v_ / 10.0f, draft_a_ / 10.0f) : 0;
    if (!id) { cancel_("Queue full; command not submitted", Result::REJECTED); return; }
    pending_id_ = last_request_id_ = id;
    mode_ = apply ? UiMode::SUBMITTING : UiMode::REFRESHING;
    feedback_(apply ? "Apply submitted once; waiting for readback" : "Refreshing configuration", Result::ACCEPTED);
  }
  void gesture_(bool a, bool long_press, uint32_t now, const Snapshot &state) {
    if (pending_id_) return;
    last_action_ = now;
    if (mode_ == UiMode::VIEW) {
      if (a && !long_press) {
        field_ = field_ == UiField::VOLTAGE ? UiField::CURRENT : UiField::VOLTAGE;
        feedback_("Hold A to edit; B refreshes");
      } else if (!a && !long_press) {
        if (!state.ready || state.busy) feedback_("Refresh unavailable while disconnected or busy", Result::REJECTED);
        else submit_(false);
      } else if (a) {
        if (!display_alive_(state, now)) feedback_("Active LCD required for local setting edits", Result::REJECTED);
        else if (!state.ready || state.busy || !valid_(state.voltage, true) || !valid_(state.current, false))
          feedback_("Require ready charger and valid readback", Result::REJECTED);
        else {
          baseline_v_ = draft_v_ = tenths_(state.voltage);
          baseline_a_ = draft_a_ = tenths_(state.current);
          last_request_id_ = 0;
          mode_ = UiMode::EDIT;
          feedback_("A -0.1 / B +0.1; hold A to review");
        }
      }
      return;
    }
    if (!a && (long_press || mode_ == UiMode::CONFIRM)) {
      cancel_("Cancelled; nothing sent", Result::INFO);
      return;
    }
    if (a && long_press) {
      if (mode_ == UiMode::EDIT) {
        if (draft_v_ == baseline_v_ && draft_a_ == baseline_a_) {
          cancel_("Unchanged; nothing sent", Result::INFO);
        } else {
          mode_ = UiMode::CONFIRM;
          feedback_("Hold A again to Apply; B cancels");
        }
      } else if (mode_ == UiMode::CONFIRM) submit_(true);
      return;
    }
    if (mode_ == UiMode::EDIT && !long_press) {
      int &value = field_ == UiField::VOLTAGE ? draft_v_ : draft_a_;
      const int minimum = field_ == UiField::VOLTAGE ? 582 : 49;
      const int maximum = field_ == UiField::VOLTAGE ? 584 : 51;
      const int next = value + (a ? -1 : 1);
      if (next < minimum || next > maximum) feedback_("Captured limit reached", Result::REJECTED);
      else { value = next; feedback_("Draft only; hold A to review"); }
    }
  }
  Request request_;
  UiMode mode_{UiMode::VIEW};
  UiField field_{UiField::VOLTAGE};
  Result result_{Result::INFO};
  std::string message_{"A selects; B refreshes; hold A edits"};
  int draft_v_{584}, draft_a_{51}, baseline_v_{584}, baseline_a_{51};
  uint32_t last_action_{0}, started_a_{0}, started_b_{0}, last_sequence_{0};
  uint32_t pending_id_{0}, last_request_id_{0};
  uint32_t display_seen_at_{0};
  bool display_seen_{false};
  bool held_a_{false}, held_b_{false}, chord_{false}, sequence_seen_{false}, changed_{true};
};
}
