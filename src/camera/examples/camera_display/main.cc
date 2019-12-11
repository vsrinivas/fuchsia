// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <src/lib/syslog/cpp/logger.h>
#include <src/lib/ui/base_view/view_provider_component.h>

#include "demo_view.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"camera_display"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Chaos mode adds random delays between frame acquisition, presentation, and release.
  bool chaos = false;
  bool image_io = false;
  for (auto i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--chaos") {
      std::cout << "Chaos mode enabled!" << std::endl;
      chaos = true;
    } else if (std::string(argv[i]) == "--io") {
      std::cout << "Image IO enabled!" << std::endl;
      image_io = true;
    }
  }
  scenic::ViewProviderComponent component(
      [&loop, chaos, image_io](scenic::ViewContext context) {
        return camera::DemoView::Create(std::move(context), &loop, chaos, image_io);
      },
      &loop);
  zx_status_t status = loop.Run();
  if (status != ZX_ERR_CANCELED) {
    FX_LOGS(WARNING) << "Main thread terminated abnormally";
    return status == ZX_OK ? -1 : status;
  }
  return 0;
}
