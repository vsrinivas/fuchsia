// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple_camera_server_app.h"

#include <lib/async-loop/cpp/loop.h>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  simple_camera::SimpleCameraApp app;
  loop.Run();
  return 0;
}
