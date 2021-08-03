// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/injector/injector.h"

#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/status.h>

#include <stack>

#include "lib/async/cpp/time.h"
#include "lib/async/default.h"

namespace input {
namespace {

// TODO(fxbug.dev/24476): Remove this.
// Turn two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

uint64_t GetCurrentMinute() {
  return async::Now(async_get_default_dispatcher()).get() / zx::min(1).get();
}

bool IsValidViewport(const input::Injector::Viewport& viewport) {
  return std::isfinite(viewport.width) && std::isfinite(viewport.height) &&
         std::isfinite(viewport.scale) && std::isfinite(viewport.x_offset) &&
         std::isfinite(viewport.y_offset) && viewport.width >= 0.f && viewport.height >= 0.f;
}

}  // namespace

using fuchsia::ui::pointerinjector::EventPhase;

InjectorInspector::InjectorInspector(inspect::Node node)
    : node_(std::move(node)),
      history_stats_node_(node_.CreateLazyValues("Injection history",
                                                 [this] {
                                                   inspect::Inspector insp;
                                                   ReportStats(insp);
                                                   return fpromise::make_ok_promise(
                                                       std::move(insp));
                                                 })),
      cancelled_injections_node_(node_.CreateChild("cancelled_injections")),
      total_cancelled_injections_(
          cancelled_injections_node_.CreateUint("total_cancelled_injections", 0)),
      injection_in_flight_count_(
          cancelled_injections_node_.CreateUint("injection_in_flight_count", 0)),
      pending_events_empty_count_(
          cancelled_injections_node_.CreateUint("pending_events_empty_count", 0)),
      scene_not_ready_count_(cancelled_injections_node_.CreateUint("scene_not_ready_count", 0)) {}

void InjectorInspector::OnInjectedEvents(uint64_t num_events) {
  const uint64_t current_minute = GetCurrentMinute();

  // Add elements to the front and pop from the back so that the newest element will be read out
  // first when we later iterate over the deque.
  if (history_.empty() || history_.front().minute_key != current_minute) {
    history_.push_front({
        .minute_key = current_minute,
    });
  }
  history_.front().num_injected_events += num_events;

  // Pop off everything older than |kNumMinutesOfHistory|.
  while (history_.size() > 1 &&
         history_.back().minute_key + kNumMinutesOfHistory <= current_minute) {
    history_.pop_back();
  }
}

void InjectorInspector::OnInjectPendingCancelled(bool injection_in_flight,
                                                 bool pending_events_empty, bool scene_not_ready) {
  FX_DCHECK(injection_in_flight || pending_events_empty || scene_not_ready)
      << "Should only cancel an inject with one or more valid reasons";

  total_cancelled_injections_.Add(1);

  if (injection_in_flight) {
    injection_in_flight_count_.Add(1);
  }
  if (pending_events_empty) {
    pending_events_empty_count_.Add(1);
  }
  if (scene_not_ready) {
    scene_not_ready_count_.Add(1);
  }
}

void InjectorInspector::ReportStats(inspect::Inspector& inspector) const {
  inspect::Node node = inspector.GetRoot().CreateChild(
      "Last " + std::to_string(kNumMinutesOfHistory) + " minutes of injected events");

  uint64_t sum = 0;
  const uint64_t current_minute = GetCurrentMinute();
  for (const auto& [minute, num_injected_events] : history_) {
    if (minute + kNumMinutesOfHistory <= current_minute) {
      break;
    }

    node.CreateUint("Events at minute " + std::to_string(minute), num_injected_events, &inspector);

    sum += num_injected_events;
  }
  node.CreateUint("Sum", sum, &inspector);
  inspector.emplace(std::move(node));
}

Injector::Injector(sys::ComponentContext* component_context, fuchsia::ui::views::ViewRef context,
                   fuchsia::ui::views::ViewRef target,
                   fuchsia::ui::pointerinjector::DispatchPolicy policy, inspect::Node inspect_node)
    : component_context_(component_context),
      context_view_ref_(std::move(context)),
      target_view_ref_(std::move(target)),
      policy_(policy),
      injector_inspector_(std::move(inspect_node)) {
  FX_DCHECK(component_context_);
}

Injector::Injector() : component_context_(nullptr), injector_inspector_(inspect::Node()) {}

void Injector::SetViewport(Viewport viewport) {
  FX_LOGS(INFO) << "SetViewport: width=" << viewport.width << " height=" << viewport.height
                << " scale=" << viewport.scale << " x_offset=" << viewport.x_offset
                << " y_offset=" << viewport.y_offset;
  if (!IsValidViewport(viewport)) {
    FX_LOGS(ERROR) << "Invalid viewport";
    return;
  }
  viewport_ = viewport;

  // Update the viewport of all current injectors.
  const int64_t now = async_now(async_get_default_dispatcher());
  for (auto& [id, injector] : injectors_) {
    fuchsia::ui::pointerinjector::Event event;
    {
      event.set_timestamp(now);
      event.set_trace_flow_id(TRACE_NONCE());
      fuchsia::ui::pointerinjector::Data data;
      data.set_viewport(GetCurrentViewport());
      event.set_data(std::move(data));
    }
    injector.pending_events.push_back(std::move(event));
  }
}

void Injector::OnDeviceAdded(uint32_t device_id) {
  const InjectorId injector_id = next_injector_id_++;
  injector_id_by_device_id_[device_id] = injector_id;
  SetupInputInjection(injector_id, device_id);
}

void Injector::OnDeviceRemoved(uint32_t device_id) {
  FX_DCHECK(injector_id_by_device_id_.count(device_id));

  // Clean up the corresponding injector.
  const InjectorId injector_id = injector_id_by_device_id_[device_id];
  injector_id_by_device_id_.erase(device_id);

  FX_DCHECK(injectors_.count(injector_id));
  if (injectors_[injector_id].pending_events.empty()) {
    injectors_.erase(injector_id);
  } else {
    // If we have pending events, mark it to be killed when all pending events have been handled.
    injectors_[injector_id].kill_when_empty = true;
  }
}

void Injector::InjectPending(InjectorId injector_id) {
  TRACE_DURATION("input", "inject_pending_events");
  FX_DCHECK(injectors_.count(injector_id));
  auto& injector = injectors_.at(injector_id);

  // For the first event, skip until the first ADD.
  if (injector.injecting_first_event) {
    auto& pending_events = injector.pending_events;
    std::stack<fuchsia::ui::pointerinjector::Event> non_pointer_events;
    while (!pending_events.empty()) {
      if (!pending_events.front().data().is_pointer_sample()) {
        non_pointer_events.emplace(std::move(pending_events.front()));
      } else if (pending_events.front().data().pointer_sample().phase() == EventPhase::ADD) {
        break;
      }
      pending_events.pop_front();
    }
    // Add the non-pointer events back on the queue.
    while (!non_pointer_events.empty()) {
      pending_events.emplace_front(std::move(non_pointer_events.top()));
      non_pointer_events.pop();
    }
  }

  if (injector.injection_in_flight || injector.pending_events.empty() || !scene_ready_) {
    injector_inspector_.OnInjectPendingCancelled(injector.injection_in_flight,
                                                 injector.pending_events.empty(), !scene_ready_);
    return;
  }

  injector.injection_in_flight = true;
  injector.injecting_first_event = false;

  std::vector<fuchsia::ui::pointerinjector::Event> events_to_inject;
  while (!injector.pending_events.empty() &&
         events_to_inject.size() < fuchsia::ui::pointerinjector::MAX_INJECT) {
    events_to_inject.emplace_back(std::move(injector.pending_events.front()));
    injector.pending_events.pop_front();
  }

  for (const auto& event : events_to_inject) {
    TRACE_FLOW_BEGIN("input", "dispatch_event_to_scenic", event.trace_flow_id());
  }

  injector_inspector_.OnInjectedEvents(events_to_inject.size());
  injector.touch_injector->Inject(std::move(events_to_inject), [this, injector_id] {
    if (num_failed_injection_attempts_ > 0) {
      FX_LOGS(INFO) << "Injection successful after " << num_failed_injection_attempts_
                    << " failed attempts.";
      num_failed_injection_attempts_ = 0;
    }

    FX_DCHECK(injectors_.count(injector_id));
    auto& injector = injectors_.at(injector_id);
    injector.injection_in_flight = false;
    // Drain the queue eagerly, instead of draining lazily (i.e. on receiving the next input
    // event).
    if (!injector.pending_events.empty()) {
      InjectPending(injector_id);
    } else if (injector.kill_when_empty) {
      injectors_.erase(injector_id);
    }
  });
}

// Both the API for injecting into RootPresenter and the API for injecting into Scenic support
// vector-based reporting of contemporaneous events, but DeviceState doesn't support vector
// passthrough, so injection into Scenic may not be aligned on timestamp boundaries.
void Injector::OnEvent(const fuchsia::ui::input::InputEvent& event) {
  TRACE_DURATION("input", "presentation_on_event");
  FX_VLOGS(1) << "OnEvent " << event;

  if (!event.is_pointer()) {
    FX_LOGS(ERROR) << "Received unexpected event: \"" << event
                   << "\". Only pointer input events are handled.";
    return;
  }

  const auto& pointer = event.pointer();
  const uint32_t device_id = pointer.device_id;
  FX_DCHECK(injector_id_by_device_id_.count(device_id));
  const InjectorId injector_id = injector_id_by_device_id_[device_id];

  FX_DCHECK(injectors_.count(injector_id));

  // TODO(fxbug.dev/24476): Use proper trace_id for tracing flow.
  const trace_flow_id_t trace_id = PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
  TRACE_FLOW_END("input", "dispatch_event_to_presentation", trace_id);

  EventPhase phase;
  switch (pointer.phase) {
    case fuchsia::ui::input::PointerEventPhase::ADD:
      phase = EventPhase::ADD;
      break;
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      phase = EventPhase::CHANGE;
      break;
    case fuchsia::ui::input::PointerEventPhase::REMOVE:
      phase = EventPhase::REMOVE;
      break;
    case fuchsia::ui::input::PointerEventPhase::CANCEL:
      phase = EventPhase::CANCEL;
      break;
    default: {
      FX_LOGS(ERROR) << "Received unexpected phase: " << pointer.phase;
      return;
    }
  }

  fuchsia::ui::pointerinjector::Event out_event;
  {
    out_event.set_timestamp(pointer.event_time);
    out_event.set_trace_flow_id(trace_id);
    {
      fuchsia::ui::pointerinjector::PointerSample pointer_sample;
      pointer_sample.set_pointer_id(pointer.pointer_id);
      pointer_sample.set_phase(phase);
      pointer_sample.set_position_in_viewport({pointer.x, pointer.y});
      fuchsia::ui::pointerinjector::Data data;
      data.set_pointer_sample(std::move(pointer_sample));
      out_event.set_data(std::move(data));
    }
  }

  injectors_.at(injector_id).pending_events.emplace_back(std::move(out_event));
  InjectPending(injector_id);
}

fuchsia::ui::pointerinjector::Viewport Injector::GetCurrentViewport() const {
  fuchsia::ui::pointerinjector::Viewport viewport;
  std::array<std::array<float, 2>, 2> extents{{/*min*/ {0, 0},
                                               /*max*/ {viewport_.width, viewport_.height}}};
  viewport.set_extents(extents);
  viewport.set_viewport_to_context_transform(std::array<float, 9>{
      viewport_.scale, 0, 0,                      // first column
      0, viewport_.scale, 0,                      // second column
      viewport_.x_offset, viewport_.y_offset, 1,  // third column
  });

  return viewport;
}

void Injector::SetupInputInjection(InjectorId injector_id, uint32_t device_id) {
  auto& injector = injectors_[injector_id];
  injector.device_id = device_id;
  if (!scene_ready_)
    return;

  fuchsia::ui::pointerinjector::Config config;
  {
    config.set_device_id(device_id);
    config.set_device_type(fuchsia::ui::pointerinjector::DeviceType::TOUCH);
    // TOP_HIT_AND_ANCESTORS_IN_TARGET means only views from |target_| down may receive events. The
    // events may go to the view with the top hit and its ancestors up to and including |target_|.
    // The final decision on who gets the event is determined by Scenic and client protocols.
    // In the case of EXCLUSIVE_TARGET, the events are sent directly to |target_|.
    config.set_dispatch_policy(policy_);
    config.set_viewport(GetCurrentViewport());
    {
      fuchsia::ui::pointerinjector::Context context;
      fuchsia::ui::views::ViewRef context_clone;
      fidl::Clone(context_view_ref_, &context_clone);
      context.set_view(std::move(context_clone));
      config.set_context(std::move(context));
    }
    {
      fuchsia::ui::pointerinjector::Target target;
      fuchsia::ui::views::ViewRef target_clone;
      fidl::Clone(target_view_ref_, &target_clone);
      target.set_view(std::move(target_clone));
      config.set_target(std::move(target));
    }
  }

  injector.injection_in_flight = false;
  injector.injecting_first_event = true;
  component_context_->svc()->Connect<fuchsia::ui::pointerinjector::Registry>()->Register(
      std::move(config), injectors_.at(injector_id).touch_injector.NewRequest(), [] {});
  injector.touch_injector.set_error_handler([this, injector_id, device_id](zx_status_t error) {
    ++num_failed_injection_attempts_;
    if (num_failed_injection_attempts_ % kLogFrequency == 1) {
      FX_LOGS(ERROR) << "Input injection channel for device id " << device_id
                     << " died with error: " << zx_status_get_string(error)
                     << ". Num failed attempts: " << num_failed_injection_attempts_
                     << ". Attempting recovery.";
    }

    // Move the binding onto the stack so we can replace it safely from within the closure.
    const auto old_device_ptr = std::move(injectors_[injector_id].touch_injector);
    injectors_[injector_id].touch_injector = {};

    // Try to recover.
    SetupInputInjection(injector_id, device_id);
    InjectPending(injector_id);
  });
}

void Injector::MarkSceneReady() {
  if (scene_ready_) {
    return;
  }

  scene_ready_ = true;
  for (const auto& [id, injector] : injectors_) {
    SetupInputInjection(id, injector.device_id);
    InjectPending(id);
  }
}

}  // namespace input
