// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "lib/escher/escher_process_init.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  escher::GlslangInitializeProcess();
  int result = RUN_ALL_TESTS();
  escher::GlslangFinalizeProcess();
  return result;
}
