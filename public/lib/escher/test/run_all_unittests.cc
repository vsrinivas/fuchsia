// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/escher_process_init.h"
#include "lib/escher/test/gtest_escher.h"

// OS_FUCHSIA is defined in build_config.h.
#include "lib/fxl/build_config.h"
#ifdef OS_FUCHSIA
#include <lib/async-loop/cpp/loop.h>
#endif

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  escher::test::SetUpEscher();
#ifdef OS_FUCHSIA
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
#endif
  int result = RUN_ALL_TESTS();
  escher::test::TearDownEscher();
  return result;
}
