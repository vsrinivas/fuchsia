// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/packages/biscotti_guest/linux_runner/linux_runner.h"

#include <memory>

#include "src/lib/fxl/logging.h"

namespace linux_runner {

static constexpr off_t kStatefulImageSize = 4000ul * 1024 * 1024;

LinuxRunner::LinuxRunner() : context_(sys::ComponentContext::Create()) {
  context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

zx_status_t LinuxRunner::Init() {
  TRACE_DURATION("linux_runner", "LinuxRunner::Init");
  GuestConfig config{
      .stateful_image_size = kStatefulImageSize,
  };
  return Guest::CreateAndStart(context_.get(), config, &guest_);
}

void LinuxRunner::StartComponent(
    fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  TRACE_DURATION("linux_runner", "LinuxRunner::StartComponent");
  AppLaunchRequest request = {
      std::move(application),
      std::move(startup_info),
      std::move(controller),
  };
  guest_->Launch(std::move(request));
}

}  // namespace linux_runner
