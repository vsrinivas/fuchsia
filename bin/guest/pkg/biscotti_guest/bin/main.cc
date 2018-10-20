// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/logging.h>

#include "garnet/bin/guest/pkg/biscotti_guest/bin/guest.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  std::unique_ptr<biscotti::Guest> guest;
  zx_status_t status = biscotti::Guest::CreateAndStart(context.get(), &guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start guest: " << status;
    return -1;
  }
  loop.Run();
  return 0;
}
