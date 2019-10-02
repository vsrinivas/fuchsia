// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <zxtest/zxtest.h>

namespace {

double fpu_test_loop(long ex_loops, long factor) {
  double ev[4] = {1.0, -1.0, -1.0, -1.0};
  double t =  0.49999975;

  const long inner = 120 * factor;

  for (int ix = 0; ix < ex_loops; ix++) {
    for (int iz = 0; iz < inner; iz++) {
          ev[0] = (ev[0] + ev[1] + ev[2] - ev[3]) * t;
          ev[1] = (ev[0] + ev[1] - ev[2] + ev[3]) * t;
          ev[2] = (ev[0] - ev[1] + ev[2] + ev[3]) * t;
          ev[3] = (-ev[0] + ev[1] + ev[2] + ev[3]) * t;
    }
    t = 1.0 - t;
  }
  return ev[3];
}

// This is a floating point computation that takes longer than one
// quantum. It is meant to test the code that handles saving and
// restoring the floating point registers, in particular for ARM.
// For reference, with the parameters below it takes about 500ms
// to complete in the arm-qemu-kvm bots.
TEST(FPUTest, LongComputeLoop) {
  auto result = fpu_test_loop(5, 100);
  char result_str[64] = {};
  sprintf(result_str, "%3.18f", result);
  ASSERT_EQ(strcmp("-1.123982548697285422", result_str), 0);
}

}  // namespace
