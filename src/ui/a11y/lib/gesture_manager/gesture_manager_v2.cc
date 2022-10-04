// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager_v2.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "fuchsia/ui/pointer/cpp/fidl.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"

namespace a11y {
namespace {

using A11yPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using A11yPointerEventPhase = fuchsia::ui::input::PointerEventPhase;
using fuchsia::math::PointF;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::pointer::EventPhase;
using fuchsia::ui::pointer::Rectangle;
using fuchsia::ui::pointer::TouchInteractionId;
using fuchsia::ui::pointer::TouchPointerSample;
using fuchsia::ui::pointer::TouchResponse;
using fuchsia::ui::pointer::TouchResponseType;
using fuchsia::ui::pointer::augment::TouchEventWithLocalHit;
using fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr;

// Convert an array of 2 floats into an (x, y) pair.
PointF point2ToPointF(std::array<float, 2> point) { return {point[0], point[1]}; }

// Helper for `normalize_to_ndc`.
//
// Normalize `p` to be in the square [0, 1] * [0, 1].
//
// Returns std::nullopt if p is not contained in bounds.
std::optional<PointF> normalizeToUnitSquare(PointF p, Rectangle bounds) {
  const float x_min = bounds.min[0];
  const float y_min = bounds.min[1];
  const float x_max = bounds.max[0];
  const float y_max = bounds.max[1];

  const bool in_bounds = (x_min <= p.x && p.x <= x_max) && (y_min <= p.y && p.y <= y_max);
  if (!in_bounds) {
    return std::nullopt;
  }

  const float width = x_max - x_min;
  const float height = y_max - y_min;
  const float dx = p.x - x_min;
  const float dy = p.y - y_min;

  return {{
      width > 0 ? dx / width : 0,
      height > 0 ? dy / height : 0,
  }};
}

// Normalize `p` to be in the square [-1, 1] * [-1, 1].
//
// Returns std::nullopt if p is not contained in bounds.
std::optional<PointF> normalizeToNdc(PointF p, Rectangle bounds) {
  auto normalized = normalizeToUnitSquare(p, bounds);
  if (normalized) {
    return {{
        normalized->x * 2 - 1,
        normalized->y * 2 - 1,
    }};
  }
  return std::nullopt;
}

// Convert the new Scenic `EventPhase` type to the old `PointerEventPhase` type.
A11yPointerEventPhase convertPhase(EventPhase phase) {
  switch (phase) {
    case EventPhase::ADD:
      return A11yPointerEventPhase::DOWN;
    case EventPhase::CHANGE:
      return A11yPointerEventPhase::MOVE;
    case EventPhase::REMOVE:
      return A11yPointerEventPhase::UP;
    case EventPhase::CANCEL:
      return A11yPointerEventPhase::CANCEL;
  }
}

// Convert the new Scenic `TouchEventWithLocalHit` type to the old
// `fuchsia::ui::input::accessibility::PointerEvent` type.
//
// `event` *must* be a pointer sample event.
A11yPointerEvent convertEvent(const TouchEventWithLocalHit& event, Rectangle viewport_bounds_) {
  A11yPointerEvent a11y_event;

  FX_DCHECK(event.touch_event.has_timestamp());
  a11y_event.set_event_time(event.touch_event.timestamp());

  FX_DCHECK(event.touch_event.has_pointer_sample());
  const TouchPointerSample& sample = event.touch_event.pointer_sample();
  FX_DCHECK(sample.has_interaction());
  a11y_event.set_device_id(sample.interaction().device_id);
  a11y_event.set_pointer_id(sample.interaction().pointer_id);

  a11y_event.set_type(PointerEventType::TOUCH);

  FX_DCHECK(sample.has_phase());
  a11y_event.set_phase(convertPhase(sample.phase()));

  a11y_event.set_local_point(point2ToPointF(event.local_point));
  a11y_event.set_viewref_koid(event.local_viewref_koid);

  FX_DCHECK(sample.has_position_in_viewport());
  auto p1 = sample.position_in_viewport();
  // TODO(fxbug.dev/111160): verify manually that this doesn't need an extra 90 degree rotation.
  auto p2 = normalizeToNdc(point2ToPointF(p1), viewport_bounds_);
  FX_DCHECK(p2);
  a11y_event.set_ndc_point(*p2);

  return a11y_event;
}

// Based on the current state of the a11y gesture arena, what should we tell Scenic?
//
// Note that this is only the initial response to Scenic; in some cases, we'll
// say "maybe" or "hold" to indicate we don't know whether this interaction is
// ours yet. Once the current a11y gesture arena contest completes, we go back
// and update our responses with Scenic.
TouchResponseType initialResponse(GestureArena::State arena_state, EventPhase event_phase) {
  switch (arena_state) {
    case GestureArena::State::kIdle:
      FX_DCHECK(false) << "arena was idle; you must send it an event first";
      return TouchResponseType::NO;
    case GestureArena::State::kInProgress:
      switch (event_phase) {
        case EventPhase::ADD:
        case EventPhase::CHANGE:
          return TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS;
        case EventPhase::REMOVE:
        case EventPhase::CANCEL:
          return TouchResponseType::HOLD_SUPPRESS;
      }
    case GestureArena::State::kWinnerAssigned:
    case GestureArena::State::kContestEndedWinnerAssigned:
      return TouchResponseType::YES_PRIORITIZE;
    case GestureArena::State::kContestEndedAllDefeated:
    case GestureArena::State::kAllDefeated:
      return TouchResponseType::NO;
  }
}

}  // namespace

GestureManagerV2::GestureManagerV2(TouchSourceWithLocalHitPtr touch_source,
                                   std::unique_ptr<GestureArena> arena)
    : touch_source_(std::move(touch_source)), arena_(std::move(arena)) {
  WatchForTouchEvents({});
}

void GestureManagerV2::WatchForTouchEvents(std::vector<TouchResponse> old_responses) {
  touch_source_->Watch(
      std::move(old_responses), [this](std::vector<TouchEventWithLocalHit> events) {
        std::vector<TouchResponse> event_responses(events.size());

        for (uint32_t i = 0; i < events.size(); ++i) {
          const auto& event = events[i];

          if (event.touch_event.has_device_info()) {
            FX_DCHECK(event.touch_event.device_info().has_id());
            FX_DCHECK(!touch_device_id_.has_value());
            touch_device_id_ = event.touch_event.device_info().id();
          }
          if (event.touch_event.has_view_parameters()) {
            viewport_bounds_ = event.touch_event.view_parameters().viewport;
          }

          if (event.touch_event.has_pointer_sample()) {
            FX_DCHECK(event.touch_event.has_trace_flow_id());
            auto trace_flow_id = event.touch_event.trace_flow_id();
            event_responses[i].set_trace_flow_id(trace_flow_id);

            FX_DCHECK(viewport_bounds_);
            auto a11y_event = convertEvent(event, *viewport_bounds_);

            // Dispatch the event to the gesture arena.
            // We duplicate events in certain phases to keep the old API happy.
            FX_DCHECK(a11y_event.has_phase());
            switch (a11y_event.phase()) {
              case A11yPointerEventPhase::DOWN:
                a11y_event.set_phase(A11yPointerEventPhase::ADD);
                arena_->OnEvent(a11y_event);
                a11y_event.set_phase(A11yPointerEventPhase::DOWN);
                arena_->OnEvent(a11y_event);
                break;
              case A11yPointerEventPhase::MOVE:
              case A11yPointerEventPhase::CANCEL:
                arena_->OnEvent(a11y_event);
                break;
              case A11yPointerEventPhase::UP:
                arena_->OnEvent(a11y_event);
                a11y_event.set_phase(A11yPointerEventPhase::REMOVE);
                arena_->OnEvent(a11y_event);
                break;
              case A11yPointerEventPhase::ADD:
              case A11yPointerEventPhase::HOVER:
              case A11yPointerEventPhase::REMOVE:
                FX_DCHECK(false) << "unreachable; we never generate these phases";
            }
            const GestureArena::State arenaState = arena_->GetState();
            FX_DCHECK(arenaState != GestureArena::State::kIdle)
                << "the arena was idle after receiving an event";

            // Set the initial response.
            FX_DCHECK(event.touch_event.pointer_sample().has_phase());
            auto phase = event.touch_event.pointer_sample().phase();
            auto response_type = initialResponse(arenaState, phase);
            event_responses[i].set_response_type(response_type);

            FX_DCHECK(event.touch_event.pointer_sample().has_interaction());
            if (response_type == TouchResponseType::HOLD_SUPPRESS) {
              auto id = event.touch_event.pointer_sample().interaction();
              interactions_on_hold_.emplace_back(id);
            }

            auto contest_ended = arenaState == GestureArena::State::kContestEndedWinnerAssigned ||
                                 arenaState == GestureArena::State::kContestEndedAllDefeated;
            if (contest_ended) {
              for (auto interaction_id : interactions_on_hold_) {
                TouchResponse response;
                response.set_response_type(response_type);
                response.set_trace_flow_id(trace_flow_id);
                touch_source_->UpdateResponse(interaction_id, std::move(response), [] {});
              }
              interactions_on_hold_.clear();
            }
          }
        }

        WatchForTouchEvents(std::move(event_responses));
      });
}

}  // namespace a11y
