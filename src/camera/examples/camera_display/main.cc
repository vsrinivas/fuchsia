// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <src/lib/ui/base_view/view_provider_component.h>

#include "demo_view.h"

int main(int argc, const char** argv) {
  syslog::SetTags({"camera_display"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Chaos mode adds random delays between frame acquisition, presentation, and release.
  bool chaos = false;
  for (auto i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--chaos") {
      std::cout << "Chaos mode enabled!" << std::endl;
      chaos = true;
    }
  }
  scenic::ViewProviderComponent component(
      [&loop, chaos](scenic::ViewContext context) {
        return camera::DemoView::Create(std::move(context), &loop, chaos);
      },
      &loop);

  loop.Run();
  return EXIT_SUCCESS;
}
