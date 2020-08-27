// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/types.h>

#include "llvm-fuzzer.h"

int main(int argc, char **argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  LlvmFuzzerImpl llvm_fuzzer;
  zx_status_t status = llvm_fuzzer.Initialize();
  if (status != ZX_OK) {
    return status;
  }

  return loop.Run();
}
