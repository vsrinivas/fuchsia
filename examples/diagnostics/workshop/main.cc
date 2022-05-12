// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>

#include <src/lib/fxl/command_line.h>

#include "profile_store.h"

int main(int argc, const char** argv) {
  std::cout << "Starting profile store server." << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto startup = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  std::unique_ptr<ProfileStore> profile_store = std::make_unique<ProfileStore>(loop.dispatcher());
  startup->outgoing()->AddPublicService(profile_store->GetHandler());
  loop.Run();

  std::cout << "Stopping profile store server." << std::endl;
  return 0;
}
