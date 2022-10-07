// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/touch_injector.h"

#include <lib/syslog/cpp/macros.h>

namespace scenic_impl::input {

using InjectorEventPhase = fuchsia::ui::pointerinjector::EventPhase;

namespace {

InternalTouchEvent CreateCancelEvent(uint32_t device_id, uint32_t pointer_id, zx_koid_t context,
                                     zx_koid_t target) {
  InternalTouchEvent cancel_event;
  cancel_event.phase = Phase::kCancel;
  cancel_event.device_id = device_id;
  cancel_event.pointer_id = pointer_id;
  cancel_event.context = context;
  cancel_event.target = target;
  return cancel_event;
}

}  // namespace

TouchInjector::TouchInjector(inspect::Node inspect_node, InjectorSettings settings,
                             Viewport viewport,
                             fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> device,
                             fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
                                 is_descendant_and_connected,
                             fit::function<void(const InternalTouchEvent&, StreamId)> inject,
                             fit::function<void()> on_channel_closed)
    : Injector(std::move(inspect_node), settings, std::move(viewport), std::move(device),
               std::move(is_descendant_and_connected), std::move(on_channel_closed)),
      inject_(std::move(inject)) {
  FX_DCHECK(inject_);
  FX_DCHECK(settings.device_type == fuchsia::ui::pointerinjector::DeviceType::TOUCH);
}

void TouchInjector::ForwardEvent(const fuchsia::ui::pointerinjector::Event& event,
                                 StreamId stream_id) {
  FX_DCHECK(stream_id != kInvalidStreamId);
  inject_(PointerInjectorEventToInternalTouchEvent(event), stream_id);
}

InternalTouchEvent TouchInjector::PointerInjectorEventToInternalTouchEvent(
    const fuchsia::ui::pointerinjector::Event& event) const {
  const InjectorSettings& settings = Injector::settings();
  InternalTouchEvent internal_event;
  internal_event.timestamp = event.timestamp();
  internal_event.device_id = settings.device_id;

  const fuchsia::ui::pointerinjector::PointerSample& pointer_sample = event.data().pointer_sample();
  internal_event.pointer_id = pointer_sample.pointer_id();
  internal_event.viewport = viewport();
  internal_event.position_in_viewport = {pointer_sample.position_in_viewport()[0],
                                         pointer_sample.position_in_viewport()[1]};
  internal_event.context = settings.context_koid;
  internal_event.target = settings.target_koid;

  switch (pointer_sample.phase()) {
    case InjectorEventPhase::ADD: {
      internal_event.phase = Phase::kAdd;
      break;
    }
    case InjectorEventPhase::CHANGE: {
      internal_event.phase = Phase::kChange;
      break;
    }
    case InjectorEventPhase::REMOVE: {
      internal_event.phase = Phase::kRemove;
      break;
    }
    case InjectorEventPhase::CANCEL: {
      internal_event.phase = Phase::kCancel;
      break;
    }
    default: {
      FX_CHECK(false) << "unsupported phase: " << static_cast<uint32_t>(pointer_sample.phase());
      break;
    }
  }

  return internal_event;
}

void TouchInjector::CancelStream(uint32_t pointer_id, StreamId stream_id) {
  const InjectorSettings& settings = Injector::settings();
  inject_(CreateCancelEvent(settings.device_id, pointer_id, settings.context_koid,
                            settings.target_koid),
          stream_id);
}

}  // namespace scenic_impl::input
