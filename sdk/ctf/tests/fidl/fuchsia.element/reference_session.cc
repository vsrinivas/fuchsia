// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <stdlib.h>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  loop.Run();

  return EXIT_SUCCESS;
}
