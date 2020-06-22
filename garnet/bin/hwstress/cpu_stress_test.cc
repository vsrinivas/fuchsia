// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_stress.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "args.h"

namespace hwstress {
namespace {

TEST(Cpu, StressCpu) {
  // Exercise the main StressCpu for a tiny amount of time.
  StatusLine status;
  CommandLineArgs args;
  EXPECT_TRUE(StressCpu(&status, args, zx::msec(1)));
}

}  // namespace
}  // namespace hwstress
