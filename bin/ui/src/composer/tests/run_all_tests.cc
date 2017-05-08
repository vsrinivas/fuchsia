// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  mtl::MessageLoop message_loop;
  return RUN_ALL_TESTS();
}
