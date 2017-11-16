// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/agent_driver.h"
#include "peridot/bin/acquirers/story_info/story_info.h"

#include "lib/fsl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::AgentDriver<maxwell::StoryInfoAcquirer> driver(
      app_context.get(), [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
