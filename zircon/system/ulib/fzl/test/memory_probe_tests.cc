// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/memory-probe.h>

#include <zxtest/zxtest.h>

namespace {

TEST(MemoryProbeTests, probe_readwrite) {
  int valid = 0;
  EXPECT_TRUE(probe_for_read(&valid));
  EXPECT_TRUE(probe_for_write(&valid));
}

void SomeFunction() {}

TEST(MemoryProbeTests, probe_readonly) {
  // This uses the address of a function. This assumes that the code section is readable but
  // not writable.
  void* some_function = reinterpret_cast<void*>(&SomeFunction);
  EXPECT_TRUE(probe_for_read(some_function));
  EXPECT_FALSE(probe_for_write(some_function));
}

TEST(MemoryProbeTests, probe_invalid) {
  EXPECT_FALSE(probe_for_read(nullptr));
  EXPECT_FALSE(probe_for_write(nullptr));
}

}  // namespace
