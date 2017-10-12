// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/syscalls.h>

class Syscall : public benchmark::Fixture {};

BENCHMARK_F(Syscall, Null)(benchmark::State& state) {
   while (state.KeepRunning()) {
    if (zx_syscall_test_0() != 0) {
      state.SkipWithError("Unexpected value returned from syscall");
      return;
    }
  }
}

BENCHMARK_F(Syscall, ManyArgs)(benchmark::State& state) {
   while (state.KeepRunning()) {
    if (zx_syscall_test_8(1, 2, 3, 4, 5, 6, 7, 8) != 36) {
      state.SkipWithError("Unexpected value returned from syscall");
      return;
    }
  }
}
