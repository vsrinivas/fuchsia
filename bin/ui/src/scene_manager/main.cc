// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/escher/examples/common/demo_harness_fuchsia.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings_command_line.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/mozart/src/scene_manager/display_watcher.h"
#include "apps/mozart/src/scene_manager/renderer/display_renderer.h"
#include "apps/mozart/src/scene_manager/scene_manager_app.h"

using namespace scene_manager;

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  SceneManagerApp::Params params;
  if (!params.Setup(command_line))
    return 1;

  mtl::MessageLoop loop;

  std::unique_ptr<SceneManagerApp> scene_manager_app;

  // Don't initialize Vulkan and the SceneManagerApp until display is ready.
  std::unique_ptr<DisplayWatcher> display_watcher(DisplayWatcher::New(
      [&scene_manager_app, &params](bool success, uint32_t width,
                                    uint32_t height, float pixel_ratio) {
        if (!success) {
          exit(1);
        }

        uint32_t multiple = DisplayRenderer::kRequiredSwapchainPixelMultiple;
        if (width % multiple != 0) {
          // Round up to the nearest multiple.
          uint32_t new_width = multiple * (width / multiple) + multiple;
          FTL_LOG(WARNING) << "Mozart SceneManager: Screen width " << width
                           << " is not a multiple of " << multiple
                           << ", rounding up to " << new_width << ".";
          width = new_width;
        }
        if (height % multiple != 0) {
          // Round up to the nearest multiple.
          uint32_t new_height = multiple * (height / multiple) + multiple;
          FTL_LOG(WARNING) << "Mozart SceneManager: Screen width " << height
                           << " is not a multiple of " << multiple
                           << ", rounding up to " << new_height << ".";
          height = new_height;
        }

        // Initialize the SceneManager.
        auto harness =
            DemoHarness::New(DemoHarness::WindowParams{"Mozart SceneManager",
                                                       width, height, 2, false},
                             DemoHarness::InstanceParams());

        app::ApplicationContext* application_context =
            static_cast<DemoHarnessFuchsia*>(harness.get())
                ->application_context();
        scene_manager_app = std::make_unique<SceneManagerApp>(
            application_context, width, height, pixel_ratio, &params,
            std::move(harness));
      }));

  loop.Run();
  return 0;
}
