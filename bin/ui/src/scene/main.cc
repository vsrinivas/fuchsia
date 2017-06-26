// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "apps/mozart/src/scene/scene_manager_app.h"
#include "lib/escher/examples/common/demo_harness_fuchsia.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

static constexpr uint32_t kScreenWidth = 2160;
static constexpr uint32_t kScreenHeight = 1440;

int main(int argc, const char** argv) {
  using namespace mozart;
  using namespace mozart::scene;

  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  SceneManagerApp::Params params;
  if (!params.Setup(command_line))
    return 1;

  mtl::MessageLoop loop;

  auto harness = DemoHarness::New(
      DemoHarness::WindowParams{"Mozart SceneManager", kScreenWidth,
                                kScreenHeight, 2, false},
      DemoHarness::InstanceParams());

  SceneManagerApp app(&params, static_cast<DemoHarnessFuchsia*>(harness.get()));
  loop.Run();
  return 0;
}
