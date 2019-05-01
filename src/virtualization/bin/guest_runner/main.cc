// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "src/lib/fxl/logging.h"
#include "src/virtualization/bin/guest_runner/runner_impl.h"

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  guest_runner::RunnerImpl runner;
  loop.Run();
}
