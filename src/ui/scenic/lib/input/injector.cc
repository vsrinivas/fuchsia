// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/injector.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/lib/glm_workaround/glm_workaround.h"
#include "src/ui/scenic/lib/input/helper.h"

namespace scenic_impl {
namespace input {

using fuchsia::ui::pointerinjector::EventPhase;

namespace {

InternalPointerEvent CreateCancelEvent(uint32_t device_id, uint32_t pointer_id, zx_koid_t context,
                                       zx_koid_t target) {
  InternalPointerEvent cancel_event;
  cancel_event.phase = Phase::CANCEL;
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

zx_status_t IsValidViewport(const fuchsia::ui::pointerinjector::Viewport& viewport) {
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
      ColumnMajorMat3VectorToMat4(viewport.viewport_to_context_transform());
  if (fabs(glm::determinant(viewport_to_context_transform)) <=
      std::numeric_limits<float>::epsilon()) {
    FX_LOGS(ERROR) << "Provided fuchsia::ui::pointerinjector::Viewport had a non-invertible matrix";
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

}  // namespace

bool Injector::IsValidConfig(const fuchsia::ui::pointerinjector::Config& config) {
  if (!config.has_device_id() || !config.has_device_type() || !config.has_context() ||
      !config.has_target() || !config.has_viewport() || !config.has_dispatch_policy()) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config| is incomplete.";
    return false;
  }

  if (config.dispatch_policy() != fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET &&
      config.dispatch_policy() !=
          fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Only EXCLUSIVE_TARGET and "
                      "TOP_HIT_AND_ANCESTORS_IN_TARGET DispatchPolicy is supported.";
    return false;
  }

  if (config.device_type() != fuchsia::ui::pointerinjector::DeviceType::TOUCH) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Only DeviceType TOUCH is supported.";
    return false;
  }

  if (!config.context().is_view() || !config.target().is_view()) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config.context| or |config.target| "
                      "is not a view. Only views are supported.";
    return false;
  }

  if (IsValidViewport(config.viewport()) != ZX_OK) {
    // Errors printed in IsValidViewport. Just return result here.
    return false;
  }

  return true;
}

Injector::Injector(InjectorSettings settings, Viewport viewport,
                   fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> device,
                   fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
                       is_descendant_and_connected,
                   fit::function<void(const InternalPointerEvent&)> inject)
    : binding_(this, std::move(device)),
      settings_(std::move(settings)),
      viewport_(std::move(viewport)),
      is_descendant_and_connected_(std::move(is_descendant_and_connected)),
      inject_(std::move(inject)) {
  FX_DCHECK(is_descendant_and_connected_);
  FX_DCHECK(inject_);
  FX_LOGS(INFO) << "Injector : Registered new injector with "
                << " Device Id: " << settings_.device_id
                << " Device Type: " << static_cast<uint32_t>(settings_.device_type)
                << " Dispatch Policy: " << static_cast<uint32_t>(settings_.dispatch_policy)
                << " Context koid: " << settings_.context_koid
                << " and Target koid: " << settings_.target_koid;
  // Set a default error handler for correct cleanup.
  SetErrorHandler([](auto...) {});
}

void Injector::SetErrorHandler(fit::function<void(zx_status_t)> error_handler) {
  binding_.set_error_handler([this, error_handler = std::move(error_handler)](zx_status_t status) {
    // Clean up ongoing streams before calling the supplied error handler.
    CancelOngoingStreams();
    error_handler(status);
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
                   .context_from_viewport_transform =
                       ColumnMajorMat3VectorToMat4(new_viewport.viewport_to_context_transform())};
      continue;
    } else if (event.data().is_pointer_sample()) {
      const auto& pointer_sample = event.data().pointer_sample();

      {
        const zx_status_t result = ValidatePointerSample(pointer_sample);
        if (result != ZX_OK) {
          CloseChannel(result);
          return;
        }
      }

      if (event.has_trace_flow_id()) {
        TRACE_FLOW_END("input", "dispatch_event_to_scenic", event.trace_flow_id());
      }

      // Translate events to internal representation and inject.
      std::vector<InternalPointerEvent> internal_events =
          PointerInjectorEventToInternalPointerEvent(event, settings_.device_id, viewport_,
                                                     settings_.context_koid, settings_.target_koid);
      for (auto& internal_event : internal_events) {
        inject_(internal_event);
      }

      continue;
    } else {
      // Should be unreachable.
      FX_LOGS(WARNING) << "Unknown fuchsia::ui::pointerinjector::Data received";
    }
  }

  callback();
}

zx_status_t Injector::ValidatePointerSample(
    const fuchsia::ui::pointerinjector::PointerSample& pointer_sample) {
  if (!HasRequiredFields(pointer_sample)) {
    FX_LOGS(ERROR)
        << "Injected fuchsia::ui::pointerinjector::PointerSample was missing required fields";
    return ZX_ERR_INVALID_ARGS;
  }

  const auto [x, y] = pointer_sample.position_in_viewport();
  if (!std::isfinite(x) || !std::isfinite(y)) {
    FX_LOGS(ERROR) << "fuchsia::ui::pointerinjector::PointerSample contained a NaN or inf value";
    return ZX_ERR_INVALID_ARGS;
  }

  // Enforce event stream ordering rules.
  if (!ValidateEventStream(pointer_sample.pointer_id(), pointer_sample.phase())) {
    FX_LOGS(ERROR) << "Inject() called with invalid event stream";
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

bool Injector::ValidateEventStream(uint32_t pointer_id, EventPhase phase) {
  const bool stream_is_ongoing = ongoing_streams_.count(pointer_id) > 0;
  const bool double_add = stream_is_ongoing && phase == EventPhase::ADD;
  const bool invalid_start = !stream_is_ongoing && phase != EventPhase::ADD;
  if (double_add || invalid_start) {
    return false;
  }

  // Update stream state.
  if (phase == EventPhase::ADD) {
    ongoing_streams_.insert(pointer_id);
  } else if (phase == EventPhase::REMOVE || phase == EventPhase::CANCEL) {
    ongoing_streams_.erase(pointer_id);
  }

  return true;
}

void Injector::CancelOngoingStreams() {
  // Inject CANCEL event for each ongoing stream.
  for (auto pointer_id : ongoing_streams_) {
    inject_(CreateCancelEvent(settings_.device_id, pointer_id, settings_.context_koid,
                              settings_.target_koid));
  }
  ongoing_streams_.clear();
}

void Injector::CloseChannel(zx_status_t epitaph) {
  CancelOngoingStreams();
  // NOTE: Triggers destruction of this object.
  binding_.Close(epitaph);
}

}  // namespace input
}  // namespace scenic_impl
