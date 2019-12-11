// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "simple_camera_server_app.h"
#include "src/lib/syslog/cpp/logger.h"

int main() {
  syslog::InitLogger({"simple_camera_server"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  simple_camera::SimpleCameraApp app;
  loop.Run();
  return 0;
}
