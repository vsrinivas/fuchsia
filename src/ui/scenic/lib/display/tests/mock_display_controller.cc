// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"

namespace scenic_impl {
namespace display {
namespace test {

DisplayControllerObjects CreateMockDisplayController() {
  DisplayControllerObjects controller_objs;

  zx::channel controller_channel_server;
  zx::channel controller_channel_client;
  FX_CHECK(ZX_OK == zx::channel::create(0, &controller_channel_server, &controller_channel_client));

  controller_objs.mock = std::make_unique<MockDisplayController>();
  controller_objs.mock->Bind(std::move(controller_channel_server));

  controller_objs.interface_ptr = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
  controller_objs.interface_ptr->Bind(std::move(controller_channel_client));
  controller_objs.listener =
      std::make_unique<DisplayControllerListener>(controller_objs.interface_ptr);

  return controller_objs;
}

}  // namespace test
}  // namespace display
}  // namespace scenic_impl
