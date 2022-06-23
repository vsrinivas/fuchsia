// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <random>
#include <thread>

int main() {
  // This is the minimal reproducer used for the issue described in fxbug.dev/66819. We have this in
  // its own separate executable so it's easier to spawn in a new process. This needs to run in a
  // separate process each time so lsan's atexit handler can be called while the detached thread can
  // still run.
  std::thread t([] {
    std::uniform_int_distribution<int> dist(0, 1000);
    std::random_device random;
    usleep(dist(random));
  });
  t.detach();
  return 0;
}
