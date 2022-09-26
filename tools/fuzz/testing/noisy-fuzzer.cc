// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <stddef.h>
#include <stdint.h>

#include <iomanip>
#include <iostream>
#include <sstream>

// A simple fuzzer that emits a lot of noise to stdout and syslog.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  zx::nanosleep(zx::deadline_after(zx::msec(1)));
  std::ostringstream oss;
  oss << std::hex;
  for (auto i = 0U; i < size; ++i) {
    oss << std::setw(2) << std::setfill('0') << size_t(data[i]);
  }
  auto hex = oss.str();
  std::cout << "stdout-noise: " << hex << std::endl;
  FX_LOGS(INFO) << "syslog-noise: " << hex;
  return 0;
}
