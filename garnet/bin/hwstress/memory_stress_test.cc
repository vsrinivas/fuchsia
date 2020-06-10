// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_stress.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(Memory, StressMemory) {
  // Exercise the main StressMemory function for a tiny amount of time.
  StressMemory(zx::msec(1));
}

}  // namespace
}  // namespace hwstress
