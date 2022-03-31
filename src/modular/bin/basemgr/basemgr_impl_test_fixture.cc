// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/basemgr_impl_test_fixture.h"

namespace modular {

void FakeComponentWithNamespace::Register(std::string url,
                                          sys::testing::FakeLauncher& fake_launcher,
                                          async_dispatcher_t* dispatcher) {
  fake_launcher.RegisterComponent(
      std::move(url),
      [this, dispatcher](fuchsia::sys::LaunchInfo launch_info,
                         fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        ctrls_.push_back(std::move(ctrl));
        zx_status_t status =
            directory_.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                             std::move(launch_info.directory_request), dispatcher);
        ZX_ASSERT(status == ZX_OK);

        namespace_map_.clear();
        for (size_t i = 0; i < launch_info.flat_namespace->paths.size(); ++i) {
          namespace_map_.emplace(launch_info.flat_namespace->paths[i],
                                 std::move(launch_info.flat_namespace->directories[i]));
        }
        launch_count_++;
      });
}

}  // namespace modular
