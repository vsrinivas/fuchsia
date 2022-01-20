// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/mouse_injector.h"

#include <lib/syslog/cpp/macros.h>

namespace scenic_impl::input {

namespace {

ScrollInfo CreateScrollInfo(const fuchsia::input::report::Axis& axis,
                            std::optional<int64_t> scroll_value) {
  ScrollInfo scroll_info = {
      .unit = axis.unit.type,
      .exponent = axis.unit.exponent,
      .range = {axis.range.min, axis.range.max},
  };

  if (scroll_value.has_value()) {
    scroll_info.scroll_value = scroll_value.value();
  }

  return scroll_info;
}

}  // namespace

MouseInjector::MouseInjector(inspect::Node inspect_node, InjectorSettings settings,
                             Viewport viewport,
                             fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> device,
                             fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
                                 is_descendant_and_connected,
                             fit::function<void(const InternalMouseEvent&, StreamId)> inject,
                             fit::function<void(StreamId stream_id)> cancel_stream,
                             fit::function<void()> on_channel_closed)
    : Injector(std::move(inspect_node), settings, std::move(viewport), std::move(device),
               std::move(is_descendant_and_connected), std::move(on_channel_closed)),
      inject_(std::move(inject)),
      cancel_stream_(std::move(cancel_stream)) {
  FX_DCHECK(inject_);
  FX_DCHECK(settings.device_type == fuchsia::ui::pointerinjector::DeviceType::MOUSE);
  FX_DCHECK(settings.button_identifiers.size() > 0) << "Tried to add a mouse with no buttons";
}

void MouseInjector::ForwardEvent(const fuchsia::ui::pointerinjector::Event& event,
                                 StreamId stream_id) {
  {  // For CANCEL and REMOVE phase we need to cancel the stream. Otherwise inject normally.
    FX_DCHECK(event.has_data());
    const auto& data = event.data();
    if (data.is_pointer_sample()) {
      FX_DCHECK(data.pointer_sample().has_phase());
      const auto phase = data.pointer_sample().phase();
      if (phase == fuchsia::ui::pointerinjector::EventPhase::CANCEL ||
          phase == fuchsia::ui::pointerinjector::EventPhase::REMOVE) {
        cancel_stream_(stream_id);
        return;
      }
    }
  }

  inject_(PointerInjectorEventToInternalMouseEvent(event), stream_id);
}

InternalMouseEvent MouseInjector::PointerInjectorEventToInternalMouseEvent(
    const fuchsia::ui::pointerinjector::Event& event) const {
  FX_DCHECK(event.has_data());
  FX_DCHECK(event.data().is_pointer_sample());

  InternalMouseEvent internal_event;
  const InjectorSettings& settings = Injector::settings();

  // General
  internal_event.timestamp = event.timestamp();
  internal_event.device_id = settings.device_id;
  internal_event.context = settings.context_koid;
  internal_event.target = settings.target_koid;

  const fuchsia::ui::pointerinjector::PointerSample& pointer_sample = event.data().pointer_sample();
  // Coordinates
  internal_event.viewport = viewport();
  internal_event.position_in_viewport = {pointer_sample.position_in_viewport()[0],
                                         pointer_sample.position_in_viewport()[1]};

  // Buttons
  internal_event.buttons = {.identifiers = settings.button_identifiers};
  if (pointer_sample.has_pressed_buttons()) {
    internal_event.buttons.pressed = pointer_sample.pressed_buttons();
  }

  // Scroll V
  if (settings.scroll_v_range.has_value()) {
    std::optional<int64_t> scroll_value;
    if (pointer_sample.has_scroll_v()) {
      scroll_value = pointer_sample.scroll_v();
    }
    internal_event.scroll_v = CreateScrollInfo(settings.scroll_v_range.value(), scroll_value);
  }

  // Scroll H
  if (settings.scroll_h_range.has_value()) {
    std::optional<int64_t> scroll_value;
    if (pointer_sample.has_scroll_h()) {
      scroll_value = pointer_sample.scroll_h();
    }
    internal_event.scroll_h = CreateScrollInfo(settings.scroll_h_range.value(), scroll_value);
  }

  // Relative Motion
  if (pointer_sample.has_relative_motion()) {
    internal_event.relative_motion = {pointer_sample.relative_motion()[0],
                                      pointer_sample.relative_motion()[1]};
  }

  return internal_event;
}

void MouseInjector::CancelStream(uint32_t pointer_id, StreamId stream_id) {
  cancel_stream_(stream_id);
}

}  // namespace scenic_impl::input
