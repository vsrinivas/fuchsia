// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/escher_process_init.h"
#include "src/ui/lib/escher/test/gtest_escher.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  escher::test::EscherEnvironment::RegisterGlobalTestEnvironment();
  int result = RUN_ALL_TESTS();

  return result;
}
