// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/perftest.h>

int main(int argc, char** argv) {
  return perftest::PerfTestMain(argc, argv, "driver_runtime.microbenchmarks");
}
