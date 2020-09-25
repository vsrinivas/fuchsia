// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/ui/input/gesture_detector.h"

#include <lib/syslog/cpp/macros.h>

#include <glm/gtx/norm.hpp>

namespace input {
namespace {

GestureDetector::TapType ClassifyTap(const fuchsia::ui::input::PointerEvent& event,
                                     const Gesture& state) {
  // TODO(fxbug.dev/18121): Allow custom mappings.
  switch (event.type) {
    case fuchsia::ui::input::PointerEventType::MOUSE:
      if (event.buttons == fuchsia::ui::input::kMouseTertiaryButton) {
        // Map the tertiary mouse button to the same tap type (3) as left +
        // right click.
        return 3;
      } else {
        return event.buttons;
      }
    case fuchsia::ui::input::PointerEventType::TOUCH:
      return state.pointer_count();
    case fuchsia::ui::input::PointerEventType::STYLUS:
      // For stylus, map the buttonless case to tap type 1, and decorate with
      // buttons.
      return 1 + event.buttons;
    case fuchsia::ui::input::PointerEventType::INVERTED_STYLUS:
      // When the stylus is inverted, bump the tap type by 1 (e.g. the
      // buttonless case becomes tap type 2).
      return 2 + event.buttons;
  }
}

#ifndef NDEBUG

// A delegating |GestureDetector::Interaction| wrapper that checks that
// |tap_type > 0|.
class CheckedInteraction : public GestureDetector::Interaction {
 public:
  CheckedInteraction(std::unique_ptr<GestureDetector::Interaction> interaction)
      : interaction_(std::move(interaction)) {}

 private:
  void OnTapBegin(const glm::vec2& coordinate, GestureDetector::TapType tap_type) override {
    FX_CHECK(tap_type > 0);
    interaction_->OnTapBegin(coordinate, tap_type);
  }

  void OnTapUpdate(GestureDetector::TapType tap_type) override {
    FX_CHECK(tap_type > 0);
    interaction_->OnTapUpdate(tap_type);
  }

  void OnTapCommit() override { interaction_->OnTapCommit(); }

  void OnMultidrag(GestureDetector::TapType tap_type, const Gesture::Delta& delta) override {
    FX_CHECK(tap_type > 0);
    interaction_->OnMultidrag(tap_type, delta);
  }

  std::unique_ptr<GestureDetector::Interaction> interaction_;
};

#endif  // NDEBUG

}  // namespace

GestureDetector::Interaction::~Interaction() = default;

void GestureDetector::Interaction::OnTapBegin(const glm::vec2& coordinate,
                                              GestureDetector::TapType tap_type) {}
void GestureDetector::Interaction::OnTapUpdate(GestureDetector::TapType tap_type) {}
void GestureDetector::Interaction::OnTapCommit() {}
void GestureDetector::Interaction::OnMultidrag(GestureDetector::TapType tap_type,
                                               const Gesture::Delta& delta) {}

GestureDetector::Delegate::~Delegate() = default;

GestureDetector::DevicePointerState::DevicePointerState() : weak_ptr_factory_(this) {}

fxl::WeakPtr<GestureDetector::DevicePointerState>
GestureDetector::DevicePointerState::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

GestureDetector::GestureDetector(Delegate* delegate, float drag_threshold)
    : delegate_(delegate), drag_threshold_squared_(drag_threshold * drag_threshold) {}

void GestureDetector::OnPointerEvent(fuchsia::ui::input::PointerEvent event) {
  switch (event.phase) {
    case fuchsia::ui::input::PointerEventPhase::DOWN: {
      fxl::WeakPtr<DevicePointerState> state = devices_[event.device_id].GetWeakPtr();
      state->gesture.AddPointer(event.pointer_id, {event.x, event.y});

      if (!state->interaction) {
        std::unique_ptr<Interaction> interaction = delegate_->BeginInteraction(&state->gesture);
        // Store interaction in temporary variable so we can guard against state having been
        // destroyed in |BeginInteraction|. This can happen because the delegate can implement
        // |BeginInteraction| however it likes, including by resetting or destroying the
        // |GestureDetector|.
        if (!state) {
          return;
        }
        state->interaction = std::move(interaction);
#ifndef NDEBUG
        state->interaction = std::make_unique<CheckedInteraction>(std::move(state->interaction));
#endif
        // Only start a tap if we're the first pointer. Otherwise, if we've
        // already committed a tap, immediately enter a multidrag.
        if (state->gesture.pointer_count() == 1) {
          state->tap_type = ClassifyTap(event, state->gesture);
          state->interaction->OnTapBegin({event.x, event.y}, state->tap_type);
          if (!state) {
            return;
          }
        }
        state->origins[event.pointer_id] = {event.x, event.y};
      } else if (state->tap_type > 0) {
        TapType tap_type = ClassifyTap(event, state->gesture);
        if (tap_type > state->tap_type) {
          state->tap_type = tap_type;
          state->interaction->OnTapUpdate(tap_type);
          if (!state) {
            return;
          }
        }
        state->origins[event.pointer_id] = {event.x, event.y};
      } else {
        // This is an in-progress multidrag that we should update with the new
        // tap_type.
        state->interaction->OnMultidrag(ClassifyTap(event, state->gesture), {});
      }
      break;
    }
    case fuchsia::ui::input::PointerEventPhase::MOVE: {
      auto it = devices_.find(event.device_id);
      if (it == devices_.end()) {
        // TODO:(fxbug.dev/24628): This ignores the mouse move case, which happens
        // outside of a DOWN/UP pair.
        break;
      }

      DevicePointerState& state = it->second;
      FX_DCHECK(state.interaction);
      // Unlike in the other handlers, we don't need to guard state with a weak pointer here since
      // all the |Interaction| callbacks in this one are the last line in the method.

      Gesture::Delta delta = state.gesture.UpdatePointer(event.pointer_id, {event.x, event.y});
      if (!state.tap_type) {
        // 0 signifies in-progress multidrag (see |tap_type| docs for details).
        state.interaction->OnMultidrag(ClassifyTap(event, state.gesture), delta);
      } else {
        // Decide whether we've exceeded the threshold to start a multidrag.
        state.pending_delta += delta;

        if (glm::distance2(state.origins[event.pointer_id], {event.x, event.y}) >=
            drag_threshold_squared_) {
          // Kill the tap and handle as a multidrag from now on.
          state.tap_type = 0;
          state.origins.clear();
          state.interaction->OnMultidrag(ClassifyTap(event, state.gesture), state.pending_delta);
        }
      }
      break;
    }
    case fuchsia::ui::input::PointerEventPhase::UP: {
      auto it = devices_.find(event.device_id);
      if (it != devices_.end()) {
        fxl::WeakPtr<DevicePointerState> state = it->second.GetWeakPtr();
        if (state->tap_type > 0) {
          state->interaction->OnTapCommit();
          if (!state) {
            return;
          }
          state->tap_type = -state->tap_type;
        }
        state->gesture.RemovePointer(event.pointer_id);
        if (!state->gesture.has_pointers()) {
          devices_.erase(it);  // This destroys the interaction as well.
        } else if (!state->tap_type) {
          // If there's still a drag active, update the |tap_type|.
          state->interaction->OnMultidrag(ClassifyTap(event, state->gesture), {});
        }
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace input
