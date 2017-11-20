// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "lib/escher/escher_process_init.h"
#include "lib/fxl/build_config.h"

#ifdef OS_FUCHSIA
#include "lib/fsl/tasks/message_loop.h"
#endif

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  escher::GlslangInitializeProcess();
#ifdef OS_FUCHSIA
  fsl::MessageLoop message_loop;
#endif
  int result = RUN_ALL_TESTS();
  escher::GlslangFinalizeProcess();
  return result;
}
