// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "smart_door_memory_server_app.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  smart_door_memory::SmartDoorMemoryServerApp app;
  loop.Run();
  return 0;
}
