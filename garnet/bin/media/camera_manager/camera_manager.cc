// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/media/camera_manager/camera_manager_impl.h"

int main(int argc, const char** argv) {
  FXL_LOG(INFO) << "Camera Manager Starting";
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  camera::CameraManagerImpl app(&loop);
  loop.Run();
  return 0;
}
