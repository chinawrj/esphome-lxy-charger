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
    last_activity_ = now;
    activity_seen_ = true;
    if (sequence_seen_ && sequence != last_sequence_ + 1) {
      held_a_ = held_b_ = chord_ = false;
      set_hold_(UiHold::NONE);
      cancel_(UiNotice::INPUT_INTERRUPTED, "Input interrupted; edit cancelled", Result::REJECTED);
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
      if (mode_ == UiMode::METER) {
        mode_ = UiMode::VIEW;
        chord_ = true;  // Consume the entire wake gesture, including a long hold/chord.
        feedback_(UiNotice::NONE, "Meter closed");
        return;
      }
      if (pending_id_) chord_ = true;  // Completion during this hold must not reinterpret it on View.
      if (held_a_ && held_b_) {
        chord_ = true;
        if (mode_ == UiMode::EDIT || mode_ == UiMode::CONFIRM)
          cancel_(UiNotice::INPUT_INTERRUPTED, "Two buttons pressed; edit cancelled", Result::REJECTED);
      }
      update_hold_(now, state);
      return;
    }
    if (!held) return;
    held = false;
    set_hold_(UiHold::NONE);
    const uint32_t duration = now - started;
    if (chord_) {
      if (!held_a_ && !held_b_) chord_ = false;
      return;
    }
    if (duration < 40 || duration > 5000) return;
    gesture_(button_a, duration >= 800, now, state);
  }

  void tick(uint32_t now, const Snapshot &state) {
    const bool was_home = mode_ == UiMode::VIEW;
    if (mode_ == UiMode::HELP) {
      if (!display_alive_(state, now))
        cancel_(UiNotice::DISPLAY_UNAVAILABLE, "Display unavailable; help closed", Result::INFO);
      else if (now - last_action_ >= 30000) cancel_(UiNotice::NONE, "Help closed", Result::INFO);
    }
    if (mode_ == UiMode::EDIT || mode_ == UiMode::CONFIRM) {
      if (!display_alive_(state, now))
        cancel_(UiNotice::DISPLAY_UNAVAILABLE, "Display unavailable; edit cancelled", Result::REJECTED);
      else if (!state.ready) cancel_(UiNotice::NOT_READY, "Charger disconnected; edit cancelled", Result::REJECTED);
      else if (state.busy) cancel_(UiNotice::BUSY, "Another operation started; edit cancelled", Result::REJECTED);
      else if (!valid_(state.voltage, true) || !valid_(state.current, false) ||
               tenths_(state.voltage) != baseline_v_ || tenths_(state.current) != baseline_a_)
        cancel_(UiNotice::CONFIG_CHANGED, "Readback changed; edit cancelled", Result::REJECTED);
      else if (now - last_action_ >= 30000) cancel_(UiNotice::TIMED_OUT, "Edit timed out; nothing sent", Result::INFO);
    }
    if (mode_ == UiMode::METER && !display_alive_(state, now))
      cancel_(UiNotice::DISPLAY_UNAVAILABLE, "Display unavailable; meter closed", Result::INFO);
    if (!activity_seen_ || !was_home || mode_ != UiMode::VIEW || held_a_ || held_b_ ||
        state.busy || !display_alive_(state, now)) {
      last_activity_ = now;
      activity_seen_ = true;
    } else if (uint32_t(now - last_activity_) >= 15000) {
      mode_ = UiMode::METER;
      feedback_(UiNotice::NONE, "Idle meter");
    }
    update_hold_(now, state);
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
      last_activity_ = now;
      const UiNotice notice = pending_kind_ == RequestKind::CONNECT ?
          (event.result == Result::VERIFIED ? UiNotice::CONNECTED : UiNotice::CONNECT_FAILED) :
          event.result == Result::VERIFIED ?
          (pending_kind_ == RequestKind::APPLY ? UiNotice::APPLIED : UiNotice::REFRESHED) :
          (event.result == Result::UNKNOWN ? UiNotice::UNKNOWN :
           (event.result == Result::REJECTED ? UiNotice::REJECTED : UiNotice::FAILED));
      feedback_(notice, event.message.c_str(), event.result);
    }
    tick(now, state);
  }

  Event ui_event() const {
    Event event{}; event.type = EventType::UI_STATE;
    event.source = "buttons";
    event.ui_mode = mode_;
    event.ui_field = field_;
    event.ui_notice = notice_;
    event.ui_hold = hold_;
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
      cancel_(UiNotice::QUEUE_FULL, "Display update unavailable; edit cancelled", Result::REJECTED);
  }

 private:
  enum class RequestKind { READ, APPLY, CONNECT };
  static int tenths_(float value) { return static_cast<int>(std::round(value * 10.0f)); }
  bool display_alive_(const Snapshot &state, uint32_t now) const {
    return display_seen_ && state.ui_display_ready && now - display_seen_at_ < 3000;
  }
  static bool valid_(float value, bool voltage) {
    if (!std::isfinite(value)) return false;
    const float scaled = value * 10.0f;
    const float rounded = std::round(scaled);
    return std::fabs(scaled - rounded) < 0.001f &&
        (voltage ? rounded >= 500 && rounded <= 930 : rounded >= 49 && rounded <= 51);
  }
  void set_hold_(UiHold hold) {
    if (hold_ != hold) { hold_ = hold; changed_ = true; }
  }
  void update_hold_(uint32_t now, const Snapshot &state) {
    UiHold next = UiHold::NONE;
    if (!chord_ && !pending_id_ && mode_ != UiMode::HELP && (held_a_ || held_b_)) {
      const uint32_t duration = now - (held_a_ ? started_a_ : started_b_);
      if (duration > 5000) next = UiHold::RELEASE;
      else if (duration >= 800) {
        if (held_a_ && mode_ == UiMode::VIEW && display_alive_(state, now) && state.ready && !state.busy &&
            valid_(state.voltage, true) && valid_(state.current, false)) next = UiHold::EDIT;
        else if (held_a_ && mode_ == UiMode::EDIT) next = UiHold::REVIEW;
        else if (held_a_ && mode_ == UiMode::CONFIRM) next = UiHold::APPLY;
        else if (held_b_ && (mode_ == UiMode::EDIT || mode_ == UiMode::CONFIRM)) next = UiHold::CANCEL;
        else if (held_b_ && mode_ == UiMode::VIEW && display_alive_(state, now)) next = UiHold::HELP;
      }
    }
    set_hold_(next);
  }
  void feedback_(UiNotice notice, const char *message, Result result = Result::INFO) {
    notice_ = notice; message_ = message; result_ = result; changed_ = true;
  }
  void cancel_(UiNotice notice, const char *message, Result result) {
    if (pending_id_) return;  // An already-submitted request is never replayed/cancelled here.
    if (held_a_ || held_b_) chord_ = true;  // Cancel the current hold as well as its draft.
    mode_ = UiMode::VIEW;
    last_request_id_ = 0;
    set_hold_(UiHold::NONE);
    feedback_(notice, message, result);
  }
  void submit_(RequestKind kind) {
    const bool apply = kind == RequestKind::APPLY;
    const bool connect = kind == RequestKind::CONNECT;
    const EventType type = apply ? EventType::REQUEST_APPLY_CONFIG :
        (connect ? EventType::REQUEST_CONNECT : EventType::REQUEST_READ_CONFIG);
    const uint32_t id = request_ ? request_(type, apply ? draft_v_ / 10.0f : NAN,
                                          apply ? draft_a_ / 10.0f : NAN) : 0;
    if (!id) { cancel_(UiNotice::QUEUE_FULL, "Queue full; command not submitted", Result::REJECTED); return; }
    pending_id_ = last_request_id_ = id;
    pending_kind_ = kind;
    mode_ = apply ? UiMode::SUBMITTING : (connect ? UiMode::CONNECTING : UiMode::REFRESHING);
    feedback_(apply ? UiNotice::APPLY_PENDING : (connect ? UiNotice::CONNECT_PENDING : UiNotice::REFRESH_PENDING),
              apply ? "Apply submitted once; waiting for readback" :
              (connect ? "Connection requested once" : "Refreshing configuration"), Result::ACCEPTED);
  }
  void gesture_(bool a, bool long_press, uint32_t now, const Snapshot &state) {
    if (pending_id_) return;
    last_action_ = now;
    if (mode_ == UiMode::HELP) {
      if (!long_press) cancel_(UiNotice::NONE, "Help closed", Result::INFO);
      return;  // Returning from Help consumes the gesture; it cannot refresh/edit.
    }
    if (mode_ == UiMode::VIEW) {
      if (a && !long_press) {
        field_ = field_ == UiField::VOLTAGE ? UiField::CURRENT : UiField::VOLTAGE;
        feedback_(UiNotice::SELECTED, "Hold A to edit; B refreshes");
      } else if (!a && !long_press) {
        if (state.busy) feedback_(UiNotice::BUSY, "Wait for the current operation", Result::REJECTED);
        else if (state.ready) submit_(RequestKind::READ);
        else if (!state.connected && !state.connection_enabled) submit_(RequestKind::CONNECT);
        else feedback_(UiNotice::NOT_READY, "Connection in progress; please wait", Result::INFO);
      } else if (a) {
        if (!display_alive_(state, now)) feedback_(UiNotice::DISPLAY_UNAVAILABLE, "Active LCD required for local setting edits", Result::REJECTED);
        else if (state.busy) feedback_(UiNotice::BUSY, "Wait for the current operation", Result::REJECTED);
        else if (!state.ready || !valid_(state.voltage, true) || !valid_(state.current, false))
          feedback_(UiNotice::NOT_READY, "Require ready charger and valid readback", Result::REJECTED);
        else {
          baseline_v_ = draft_v_ = tenths_(state.voltage);
          baseline_a_ = draft_a_ = tenths_(state.current);
          last_request_id_ = 0;
          mode_ = UiMode::EDIT;
          feedback_(UiNotice::EDITING, "A -0.1 / B +0.1; hold A to review");
        }
      } else if (display_alive_(state, now)) {
        mode_ = UiMode::HELP;
        feedback_(UiNotice::NONE, "Buttons and connection guide");
      } else {
        feedback_(UiNotice::DISPLAY_UNAVAILABLE, "Active LCD required for Help", Result::INFO);
      }
      return;
    }
    if (!a && (long_press || mode_ == UiMode::CONFIRM)) {
      cancel_(UiNotice::CANCELLED, "Cancelled; nothing sent", Result::INFO);
      return;
    }
    if (a && long_press) {
      if (mode_ == UiMode::EDIT) {
        if (draft_v_ == baseline_v_ && draft_a_ == baseline_a_) {
          cancel_(UiNotice::UNCHANGED, "Unchanged; nothing sent", Result::INFO);
        } else {
          mode_ = UiMode::CONFIRM;
          feedback_(UiNotice::REVIEW, "Hold A again to Apply; B cancels");
        }
      } else if (mode_ == UiMode::CONFIRM) submit_(RequestKind::APPLY);
      return;
    }
    if (mode_ == UiMode::EDIT && !long_press) {
      int &value = field_ == UiField::VOLTAGE ? draft_v_ : draft_a_;
      const int minimum = field_ == UiField::VOLTAGE ? 500 : 49;
      const int maximum = field_ == UiField::VOLTAGE ? 930 : 51;
      const int next = value + (a ? -1 : 1);
      if (next < minimum || next > maximum) feedback_(UiNotice::LIMIT, "Configured limit reached", Result::REJECTED);
      else { value = next; feedback_(UiNotice::EDITING, "Draft only; hold A to review"); }
    }
  }
  Request request_;
  UiMode mode_{UiMode::VIEW};
  UiField field_{UiField::VOLTAGE};
  UiNotice notice_{UiNotice::NONE};
  UiHold hold_{UiHold::NONE};
  Result result_{Result::INFO};
  std::string message_{"A selects; B refreshes; hold A edits"};
  int draft_v_{584}, draft_a_{51}, baseline_v_{584}, baseline_a_{51};
  uint32_t last_action_{0}, started_a_{0}, started_b_{0}, last_sequence_{0};
  uint32_t pending_id_{0}, last_request_id_{0};
  RequestKind pending_kind_{RequestKind::READ};
  uint32_t last_activity_{0};
  bool activity_seen_{false};
  uint32_t display_seen_at_{0};
  bool display_seen_{false};
  bool held_a_{false}, held_b_{false}, chord_{false}, sequence_seen_{false}, changed_{true};
};
}
