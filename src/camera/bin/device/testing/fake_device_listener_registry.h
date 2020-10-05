// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_TESTING_FAKE_DEVICE_LISTENER_REGISTRY_H_
#define SRC_CAMERA_BIN_DEVICE_TESTING_FAKE_DEVICE_LISTENER_REGISTRY_H_

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

class FakeDeviceListenerRegistry : public fuchsia::ui::policy::DeviceListenerRegistry {
 public:
  explicit FakeDeviceListenerRegistry(async_dispatcher_t* dispatcher);
  fidl::InterfaceRequestHandler<fuchsia::ui::policy::DeviceListenerRegistry> GetHandler();
  void SendMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event);

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::ui::policy::DeviceListenerRegistry> request);

  // |fuchsia::ui::policy::DeviceListenerRegistry|
  void RegisterMediaButtonsListener(
      fuchsia::ui::policy::MediaButtonsListenerHandle listener) override;

  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<fuchsia::ui::policy::DeviceListenerRegistry> bindings_;
  std::map<uint32_t, fuchsia::ui::policy::MediaButtonsListenerPtr> listeners_;
  uint32_t listener_id_next_ = 1;
};

#endif  // SRC_CAMERA_BIN_DEVICE_TESTING_FAKE_DEVICE_LISTENER_REGISTRY_H_
