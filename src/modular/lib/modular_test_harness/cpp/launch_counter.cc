// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/launch_counter.h"

namespace modular_testing {

LaunchCounter::LaunchCounter() : weak_factory_(this){}

modular_testing::TestHarnessBuilder::InterceptOptions LaunchCounter::WrapInterceptOptions(
    modular_testing::TestHarnessBuilder::InterceptOptions options) {
  options.launch_handler =
      [weak_this = weak_factory_.GetWeakPtr(),
       original_launch_handler = std::move(options.launch_handler)](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              intercepted_component) {
        if (weak_this) {
          weak_this->launch_count_++;
        }
        original_launch_handler(std::move(startup_info), std::move(intercepted_component));
      };
  return options;
}

} // namespace modular_testing
