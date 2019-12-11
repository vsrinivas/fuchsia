// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"

namespace scenic_impl {
namespace display {
namespace test {

DisplayControllerObjects CreateMockDisplayController() {
  DisplayControllerObjects controller_objs;

  zx::channel device_channel_server;
  zx::channel device_channel_client;
  FXL_CHECK(ZX_OK == zx::channel::create(0, &device_channel_server, &device_channel_client));
  zx::channel controller_channel_server;
  zx::channel controller_channel_client;
  FXL_CHECK(ZX_OK ==
            zx::channel::create(0, &controller_channel_server, &controller_channel_client));

  controller_objs.mock = std::make_unique<MockDisplayController>();
  controller_objs.mock->Bind(std::move(device_channel_server),
                             std::move(controller_channel_server));

  zx_handle_t controller_handle = controller_channel_client.get();
  controller_objs.interface_ptr = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
  controller_objs.interface_ptr->Bind(std::move(controller_channel_client));
  controller_objs.listener = std::make_unique<DisplayControllerListener>(
      std::move(device_channel_client), controller_objs.interface_ptr, controller_handle);

  return controller_objs;
}

}  // namespace test
}  // namespace display
}  // namespace scenic_impl
