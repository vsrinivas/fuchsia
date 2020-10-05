// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/testing/fake_device_listener_registry.h"

FakeDeviceListenerRegistry::FakeDeviceListenerRegistry(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

fidl::InterfaceRequestHandler<fuchsia::ui::policy::DeviceListenerRegistry>
FakeDeviceListenerRegistry::GetHandler() {
  return fit::bind_member(this, &FakeDeviceListenerRegistry::OnNewRequest);
}

void FakeDeviceListenerRegistry::SendMediaButtonsEvent(
    fuchsia::ui::input::MediaButtonsEvent event) {
  for (auto& [id, listener] : listeners_) {
    fuchsia::ui::input::MediaButtonsEvent event_clone;
    ZX_ASSERT(event.Clone(&event_clone) == ZX_OK);
    listener.events().OnMediaButtonsEvent(std::move(event_clone));
  }
}

void FakeDeviceListenerRegistry::OnNewRequest(
    fidl::InterfaceRequest<fuchsia::ui::policy::DeviceListenerRegistry> request) {
  bindings_.AddBinding(this, std::move(request), dispatcher_);
}

void FakeDeviceListenerRegistry::RegisterMediaButtonsListener(
    fuchsia::ui::policy::MediaButtonsListenerHandle listener) {
  fuchsia::ui::policy::MediaButtonsListenerPtr listener_ptr;
  listener_ptr.set_error_handler(
      [this, id = listener_id_next_](zx_status_t status) { listeners_.erase(id); });
  ZX_ASSERT(listener_ptr.Bind(std::move(listener), dispatcher_) == ZX_OK);
  listeners_[listener_id_next_++] = std::move(listener_ptr);
}
