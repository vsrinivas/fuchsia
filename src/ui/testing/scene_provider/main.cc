// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <cstring>

#include "src/ui/testing/scene_provider/scene_provider.h"

namespace {

int run_scene_provider(int argc, const char** argv) {
  FX_LOGS(INFO) << "Scene provider starting";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  ui_testing::SceneProvider scene_provider(context.get());
  context->outgoing()->AddPublicService(scene_provider.GetSceneControllerHandler());
  context->outgoing()->AddPublicService(scene_provider.GetGraphicalPresenterHandler());

  context->outgoing()->ServeFromStartupInfo();

  loop.Run();
  return 0;

  FX_LOGS(INFO) << "Scene provider exiting";
}

}  // namespace

int main(int argc, const char** argv) { return run_scene_provider(argc, argv); }
