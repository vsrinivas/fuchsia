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

enum : uint8_t {
  kStdout = (1 << 0),
  kStderr = (1 << 1),
  kSyslog = (1 << 2),
};

uint8_t gFlags = 0;

// Set args via meta/noisy_fuzzer.cml, e.g.
//
// {
//   include = [ ... ]
//   args = [
//     "test/noisy_fuzzer",
//     <options>
//   ]
// }
//
// Options:
//   --[no-]stdout   Whether to emit stdout noise (on by default).
//   --[no-]stderr   Whether to emit stderr noise (off by default).
//   --[no-]syslog   Whether to emit syslog noise (on by default).
//
extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  char **src = *argv;
  char **dst = src;
  int m = 0;
  int n = *argc;
  gFlags = kStdout | kSyslog;
  for (int i = 0; i < n; ++i) {
    if (strcmp(*src, "--stdout") == 0) {
      gFlags |= kStdout;
    } else if (strcmp(*src, "--no-stdout") == 0) {
      gFlags &= ~kStdout;
    } else if (strcmp(*src, "--stderr") == 0) {
      gFlags |= kStderr;
    } else if (strcmp(*src, "--no-stderr") == 0) {
      gFlags &= ~kStderr;
    } else if (strcmp(*src, "--syslog") == 0) {
      gFlags |= kSyslog;
    } else if (strcmp(*src, "--no-syslog") == 0) {
      gFlags &= ~kSyslog;
    } else {
      // Pass through remaining arguments, e.g. libFuzzer flags.
      if (*src != *dst) {
        *dst = *src;
      }
      ++dst;
      ++m;
    }
    ++src;
  }
  *argc = m;
  return 0;
}

// A simple fuzzer that emits a lot of noise to stdout and syslog.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  zx::nanosleep(zx::deadline_after(zx::msec(1)));
  std::ostringstream oss;
  oss << std::hex;
  for (auto i = 0U; i < size; ++i) {
    oss << std::setw(2) << std::setfill('0') << size_t(data[i]);
  }
  auto hex = oss.str();
  if (gFlags & kStdout) {
    std::cout << "stdout-noise: " << hex << std::endl;
  }
  if (gFlags & kStderr) {
    std::cerr << "stderr-noise: " << hex << std::endl;
  }
  if (gFlags & kSyslog) {
    FX_LOGS(INFO) << "syslog-noise: " << hex;
  }
  return 0;
}
