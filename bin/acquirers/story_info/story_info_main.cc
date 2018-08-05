// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/app_driver/cpp/agent_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>

#include "peridot/bin/acquirers/story_info/story_info.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AgentDriver<maxwell::StoryInfoAcquirer> driver(
      context.get(), [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
