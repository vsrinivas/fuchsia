// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <src/lib/syslog/cpp/logger.h>
#include <src/lib/ui/base_view/view_provider_component.h>

#include "display_view.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"camera-gym"});
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  scenic::ViewProviderComponent component(
      [&](scenic::ViewContext context) {
        return camera::DisplayView::Create(std::move(context), &loop);
      },
      &loop);
  zx_status_t status = loop.Run();
  if (status != ZX_ERR_CANCELED) {
    FX_LOGS(WARNING) << "Main thread terminated abnormally";
    return status == ZX_OK ? -1 : status;
  }
  return 0;
}
