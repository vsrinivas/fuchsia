// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/injector.h"

#include <lib/fostr/fidl/fuchsia/ui/pointerinjector/formatting.h>
#include <lib/syslog/cpp/macros.h>

#include "lib/async/cpp/time.h"
#include "lib/async/default.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"
#include "src/ui/scenic/lib/input/constants.h"
#include "src/ui/scenic/lib/utils/math.h"

namespace scenic_impl::input {

using fuchsia::ui::pointerinjector::EventPhase;

namespace {

// A histogram that ranges from 1ms to ~8s.
constexpr zx::duration kLatencyHistogramFloor = zx::msec(1);
constexpr zx::duration kLatencyHistogramInitialStep = zx::msec(1);
constexpr uint64_t kLatencyHistogramStepMultiplier = 2;
constexpr size_t kLatencyHistogramBuckets = 14;

}  // namespace

InjectorInspector::InjectorInspector(inspect::Node node)
    : node_(std::move(node)),
      viewport_event_latency_(node_.CreateExponentialUintHistogram(
          "viewport_event_latency", kLatencyHistogramFloor.to_nsecs(),
          kLatencyHistogramInitialStep.to_nsecs(), kLatencyHistogramStepMultiplier,
          kLatencyHistogramBuckets)),
      pointer_event_latency_(node_.CreateExponentialUintHistogram(
          "pointer_event_latency", kLatencyHistogramFloor.to_nsecs(),
          kLatencyHistogramInitialStep.to_nsecs(), kLatencyHistogramStepMultiplier,
          kLatencyHistogramBuckets)) {}

void InjectorInspector::OnPointerInjectorEvent(const fuchsia::ui::pointerinjector::Event& event) {
  if (!event.has_data() || !event.has_timestamp()) {
    FX_LOGS(ERROR) << "OnPointerInjectorEvent() called with an incomplete event";
    return;
  }

  async_dispatcher_t* dispatcher = async_get_default_dispatcher();
  if (dispatcher == nullptr) {
    FX_LOGS(ERROR) << "pointerinjector::Event dropped from inspect metrics. "
                      "async_get_default_dispatcher() returned null.";
    return;
  }

  zx::duration latency = async::Now(dispatcher) - zx::time(event.timestamp());

  if (event.data().is_viewport()) {
    viewport_event_latency_.Insert(latency.to_nsecs());
  } else if (event.data().is_pointer_sample()) {
    pointer_event_latency_.Insert(latency.to_nsecs());
  } else {
    FX_LOGS(ERROR) << "pointerinjector::Event dropped from inspect metrics. Unexpected data type.";
  }
}

namespace {

InternalPointerEvent CreateCancelEvent(uint32_t device_id, uint32_t pointer_id, zx_koid_t context,
                                       zx_koid_t target) {
  InternalPointerEvent cancel_event;
  cancel_event.phase = Phase::kCancel;
  cancel_event.device_id = device_id;
  cancel_event.pointer_id = pointer_id;
  cancel_event.context = context;
  cancel_event.target = target;
  return cancel_event;
}

bool HasRequiredFields(const fuchsia::ui::pointerinjector::PointerSample& pointer) {
  return pointer.has_pointer_id() && pointer.has_phase() && pointer.has_position_in_viewport();
}

bool AreValidExtents(const std::array<std::array<float, 2>, 2>& extents) {
  for (auto& point : extents) {
    for (float f : point) {
      if (!std::isfinite(f)) {
        return false;
      }
    }
  }

  const float min_x = extents[0][0];
  const float min_y = extents[0][1];
  const float max_x = extents[1][0];
  const float max_y = extents[1][1];
  return std::isless(min_x, max_x) && std::isless(min_y, max_y);
}

void ChattyLog(const fuchsia::ui::pointerinjector::Event& event) {
  static uint32_t chatty = 0;
  if (chatty++ < ChattyMax()) {
    FX_LOGS(INFO) << "Injector[" << chatty << "/" << ChattyMax() << "]: " << event;
  }
}

}  // namespace

Injector::Injector(inspect::Node inspect_node, InjectorSettings settings, Viewport viewport,
                   fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> device,
                   fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
                       is_descendant_and_connected,
                   fit::function<void(const InternalPointerEvent&, StreamId)> inject,
                   fit::function<void()> on_channel_closed)
    : inspector_(std::move(inspect_node)),
      binding_(this, std::move(device)),
      settings_(std::move(settings)),
      viewport_(std::move(viewport)),
      is_descendant_and_connected_(std::move(is_descendant_and_connected)),
      inject_(std::move(inject)),
      on_channel_closed_(std::move(on_channel_closed)) {
  FX_DCHECK(is_descendant_and_connected_);
  FX_DCHECK(inject_);
  FX_LOGS(INFO) << "Injector : Registered new injector with "
                << " Device Id: " << settings_.device_id
                << " Device Type: " << static_cast<uint32_t>(settings_.device_type)
                << " Dispatch Policy: " << static_cast<uint32_t>(settings_.dispatch_policy)
                << " Context koid: " << settings_.context_koid
                << " and Target koid: " << settings_.target_koid;

  binding_.set_error_handler([this](zx_status_t) {
    // Clean up ongoing streams before calling the supplied error handler.
    CancelOngoingStreams();
    // NOTE: Triggers destruction of this object.
    on_channel_closed_();
  });
}

void Injector::Inject(std::vector<fuchsia::ui::pointerinjector::Event> events,
                      InjectCallback callback) {
  TRACE_DURATION("input", "Injector::Inject");
  // TODO(fxbug.dev/50348): Find a way to make to listen for scene graph events instead of checking
  // connectivity per injected event.
  if (!is_descendant_and_connected_(settings_.target_koid, settings_.context_koid)) {
    FX_LOGS(ERROR) << "Inject() called with Context (koid: " << settings_.context_koid
                   << ") and Target (koid: " << settings_.target_koid
                   << ") making an invalid hierarchy.";
    CloseChannel(ZX_ERR_BAD_STATE);
    return;
  }

  if (events.empty()) {
    FX_LOGS(ERROR) << "Inject() called without any events";
    CloseChannel(ZX_ERR_INVALID_ARGS);
    return;
  }

  for (const auto& event : events) {
    if (!event.has_timestamp() || !event.has_data()) {
      FX_LOGS(ERROR) << "Inject() called with an incomplete event";
      CloseChannel(ZX_ERR_INVALID_ARGS);
      return;
    }

    inspector_.OnPointerInjectorEvent(event);

    if (event.data().is_viewport()) {
      const auto& new_viewport = event.data().viewport();

      {
        const zx_status_t result = IsValidViewport(new_viewport);
        if (result != ZX_OK) {
          // Errors printed inside IsValidViewport. Just close channel here.
          CloseChannel(result);
          return;
        }
      }
      viewport_ = {.extents = {new_viewport.extents()},
                   .context_from_viewport_transform = utils::ColumnMajorMat3VectorToMat4(
                       new_viewport.viewport_to_context_transform())};
      continue;
    } else if (event.data().is_pointer_sample()) {
      const auto& pointer_sample = event.data().pointer_sample();

      const auto [result, stream_id] = ValidatePointerSample(pointer_sample);
      if (result != ZX_OK) {
        CloseChannel(result);
        return;
      }

      if (event.has_trace_flow_id()) {
        TRACE_FLOW_END("input", "dispatch_event_to_scenic", event.trace_flow_id());
      }

      ChattyLog(event);  // Scenic accepts the event, put it on chatty log.

      // Translate events to internal representation and inject.
      std::vector<InternalPointerEvent> internal_events =
          PointerInjectorEventToInternalPointerEvent(event, settings_.device_id, viewport_,
                                                     settings_.context_koid, settings_.target_koid);

      FX_DCHECK(stream_id != kInvalidStreamId);
      for (auto& internal_event : internal_events) {
        inject_(internal_event, stream_id);
      }

      continue;
    } else {
      // Should be unreachable.
      FX_LOGS(WARNING) << "Unknown fuchsia::ui::pointerinjector::Data received";
    }
  }

  callback();
}

std::pair<zx_status_t, StreamId> Injector::ValidatePointerSample(
    const fuchsia::ui::pointerinjector::PointerSample& pointer_sample) {
  if (!HasRequiredFields(pointer_sample)) {
    FX_LOGS(ERROR)
        << "Injected fuchsia::ui::pointerinjector::PointerSample was missing required fields";
    return {ZX_ERR_INVALID_ARGS, kInvalidStreamId};
  }

  const auto [x, y] = pointer_sample.position_in_viewport();
  if (!std::isfinite(x) || !std::isfinite(y)) {
    FX_LOGS(ERROR) << "fuchsia::ui::pointerinjector::PointerSample contained a NaN or inf value";
    return {ZX_ERR_INVALID_ARGS, kInvalidStreamId};
  }

  // Enforce event stream ordering rules. It keeps the event stream clean for downstream clients.
  const auto stream_id = ValidateEventStream(pointer_sample.pointer_id(), pointer_sample.phase());
  if (stream_id == kInvalidStreamId) {
    return {ZX_ERR_BAD_STATE, kInvalidStreamId};
  }

  return {ZX_OK, stream_id};
}

StreamId Injector::ValidateEventStream(uint32_t pointer_id, EventPhase phase) {
  const bool stream_is_ongoing = ongoing_streams_.count(pointer_id) > 0;
  const bool double_add = stream_is_ongoing && phase == EventPhase::ADD;
  const bool invalid_start = !stream_is_ongoing && phase != EventPhase::ADD;
  if (double_add) {
    FX_LOGS(ERROR) << "Inject() called with invalid event stream: double-add, ptr-id: "
                   << pointer_id << ", stream-event-count: " << ongoing_streams_.count(pointer_id)
                   << ", phase: " << (int)phase;
    return kInvalidStreamId;
  }
  if (invalid_start) {
    FX_LOGS(ERROR) << "Inject() called with invalid event stream: invalid-start, ptr-id: "
                   << pointer_id << ", stream-event-count: " << ongoing_streams_.count(pointer_id)
                   << ", phase: " << (int)phase;
    return kInvalidStreamId;
  }

  // Update stream state.
  StreamId stream_id = kInvalidStreamId;
  if (phase == EventPhase::ADD) {
    ongoing_streams_.emplace(pointer_id, NewStreamId());
    stream_id = ongoing_streams_.at(pointer_id);
  } else if (phase == EventPhase::REMOVE || phase == EventPhase::CANCEL) {
    stream_id = ongoing_streams_.at(pointer_id);
    ongoing_streams_.erase(pointer_id);
  } else {
    stream_id = ongoing_streams_.at(pointer_id);
  }

  FX_DCHECK(stream_id != kInvalidStreamId);
  return stream_id;
}

void Injector::CancelOngoingStreams() {
  // Inject CANCEL event for each ongoing stream.
  for (const auto [pointer_id, stream_id] : ongoing_streams_) {
    inject_(CreateCancelEvent(settings_.device_id, pointer_id, settings_.context_koid,
                              settings_.target_koid),
            stream_id);
  }
  ongoing_streams_.clear();
}

void Injector::CloseChannel(zx_status_t epitaph) {
  CancelOngoingStreams();
  binding_.Close(epitaph);
  // NOTE: Triggers destruction of this object.
  on_channel_closed_();
}

zx_status_t Injector::IsValidViewport(const fuchsia::ui::pointerinjector::Viewport& viewport) {
  if (!viewport.has_extents() || !viewport.has_viewport_to_context_transform()) {
    FX_LOGS(ERROR) << "Provided fuchsia::ui::pointerinjector::Viewport had missing fields";
    return ZX_ERR_INVALID_ARGS;
  }

  if (!AreValidExtents(viewport.extents())) {
    FX_LOGS(ERROR)
        << "Provided fuchsia::ui::pointerinjector::Viewport had invalid extents. Extents min: {"
        << viewport.extents()[0][0] << ", " << viewport.extents()[0][1] << "} max: {"
        << viewport.extents()[1][0] << ", " << viewport.extents()[1][1] << "}";
    return ZX_ERR_INVALID_ARGS;
  }

  if (std::any_of(viewport.viewport_to_context_transform().begin(),
                  viewport.viewport_to_context_transform().end(),
                  [](float f) { return !std::isfinite(f); })) {
    FX_LOGS(ERROR) << "Provided fuchsia::ui::pointerinjector::Viewport "
                      "viewport_to_context_transform contained a NaN or infinity";
    return ZX_ERR_INVALID_ARGS;
  }

  // Must be invertible, i.e. determinant must be non-zero.
  const glm::mat4 viewport_to_context_transform =
      utils::ColumnMajorMat3VectorToMat4(viewport.viewport_to_context_transform());
  if (fabs(glm::determinant(viewport_to_context_transform)) <=
      std::numeric_limits<float>::epsilon()) {
    FX_LOGS(ERROR) << "Provided fuchsia::ui::pointerinjector::Viewport had a non-invertible matrix";
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

}  // namespace scenic_impl::input
