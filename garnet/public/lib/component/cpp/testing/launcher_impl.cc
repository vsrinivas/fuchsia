// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/testing/launcher_impl.h"
#include "lib/component/cpp/testing/test_util.h"

namespace component {
namespace testing {

void LauncherImpl::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request) {
  if (!launch_info.out) {
    launch_info.out = CloneFileDescriptor(STDOUT_FILENO);
  }
  if (!launch_info.err) {
    launch_info.err = CloneFileDescriptor(STDERR_FILENO);
  }
  launcher_->CreateComponent(std::move(launch_info), std::move(request));
}

}  // namespace testing
}  // namespace component
