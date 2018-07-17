// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/guest/runner/guest_runner.h"
#include "lib/fxl/logging.h"

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  guest_runner::GuestRunner runner;
  loop.Run();
}
