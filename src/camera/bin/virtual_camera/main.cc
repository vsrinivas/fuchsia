// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/modular/cpp/agent.h>
#include <lib/sys/cpp/component_context.h>

#include "src/camera/bin/virtual_camera/virtual_camera_agent.h"

int main(int argc, char** argv) {
  // Create the VirtualCameraAgent which will serve as the agent entrypoint.
  // Start the async::Loop until the modular::Agent quits.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  modular::Agent agent(component_context->outgoing(), [&loop] { loop.Quit(); });
  camera::VirtualCameraAgent virtual_camera_agent(component_context.get());
  loop.Run();
  return 0;
}
