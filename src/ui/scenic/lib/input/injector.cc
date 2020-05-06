// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/injector.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/input/helper.h"

namespace scenic_impl {
namespace input {

using fuchsia::ui::input3::PointerEventPhase;

namespace {

fuchsia::ui::input::PointerEvent CreateCancelEvent(uint32_t device_id, uint32_t pointer_id) {
  fuchsia::ui::input::PointerEvent cancel_event;
  cancel_event.phase = fuchsia::ui::input::PointerEventPhase::CANCEL;
  cancel_event.device_id = device_id;
  cancel_event.pointer_id = pointer_id;
  return cancel_event;
}

bool HasRequiredFields(const fuchsia::ui::pointerflow::Event& event) {
  return event.has_timestamp() && event.has_pointer_id() && event.has_phase() &&
         event.has_position_x() && event.has_position_y();
}

}  // namespace

Injector::Injector(
    InjectorId id, InjectorSettings settings,
    fidl::InterfaceRequest<fuchsia::ui::pointerflow::Injector> injector,
    fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
        is_descendant_and_connected,
    fit::function<void(/*context*/ zx_koid_t, /*target*/ zx_koid_t,
                       /*context_local_event*/ const fuchsia::ui::input::PointerEvent&)>
        inject)
    : binding_(this, std::move(injector)),
      id_(id),
      settings_(std::move(settings)),
      is_descendant_and_connected_(std::move(is_descendant_and_connected)),
      inject_(std::move(inject)) {
  FX_DCHECK(is_descendant_and_connected_);
  FX_DCHECK(inject_);
  FX_LOGS(INFO) << "Injector : Registered new injector with internal id: " << id_
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

void Injector::Inject(::std::vector<fuchsia::ui::pointerflow::Event> events,
                      InjectCallback callback) {
  // TODO(50348): Find a way to make to listen for scene graph events instead of checking
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
    if (!HasRequiredFields(event)) {
      FX_LOGS(ERROR) << "Injected fuchsia::ui::pointerflow::Event was missing required fields";
      CloseChannel(ZX_ERR_INVALID_ARGS);
      return;
    }

    // Enforce event stream ordering rules.
    if (!ValidateEventStream(event.pointer_id(), event.phase())) {
      FX_LOGS(ERROR) << "Inject() called with invalid event stream";
      CloseChannel(ZX_ERR_BAD_STATE);
      return;
    }

    // Translate events to the legacy input1 protocol and inject them.
    for (const auto& translated_event :
         PointerFlowEventToGfxPointerEvent(event, settings_.device_id)) {
      inject_(settings_.context_koid, settings_.target_koid, translated_event);
    }
  }
  callback();
}

bool Injector::ValidateEventStream(uint32_t pointer_id,
                                   fuchsia::ui::input3::PointerEventPhase phase) {
  const bool stream_is_ongoing = ongoing_streams_.count(pointer_id) > 0;
  const bool double_add = stream_is_ongoing && phase == PointerEventPhase::ADD;
  const bool invalid_start = !stream_is_ongoing && phase != PointerEventPhase::ADD;
  if (double_add || invalid_start) {
    return false;
  }

  // Update stream state.
  if (phase == PointerEventPhase::ADD) {
    ongoing_streams_.insert(pointer_id);
  } else if (phase == PointerEventPhase::REMOVE || phase == PointerEventPhase::CANCEL) {
    ongoing_streams_.erase(pointer_id);
  }

  return true;
}

void Injector::CancelOngoingStreams() {
  // Inject CANCEL event for each ongoing stream.
  for (auto pointer_id : ongoing_streams_) {
    inject_(settings_.context_koid, settings_.target_koid,
            CreateCancelEvent(settings_.device_id, pointer_id));
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
