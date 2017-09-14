// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

class Thread : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    static const char pname[] = "test process";
    zx_status_t status = zx_process_create(zx_job_default(), pname,
                         sizeof(pname), 0, &proc, &vmar);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to create process");
    }
  }

  void TearDown(benchmark::State& state) override {
    zx_handle_close(proc);
    zx_handle_close(vmar);
  }

 protected:
  zx_handle_t proc;
  zx_handle_t vmar;
};


BENCHMARK_F(Thread, Create)(benchmark::State& state) {
  zx_handle_t out;
  static const char tname[] = "test thread";
  while (state.KeepRunning()) {
    if (zx_thread_create(proc, tname, sizeof(tname), 0, &out) != ZX_OK) {
      state.SkipWithError("Failed to create thread");
      return;
    }
    state.PauseTiming();
    zx_handle_close(out);
    state.ResumeTiming();
  }
}
