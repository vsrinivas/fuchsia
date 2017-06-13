// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>

class Thread : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    static const char pname[] = "test process";
    mx_status_t status = mx_process_create(mx_job_default(), pname,
                         sizeof(pname), 0, &proc, &vmar);
    if (status != MX_OK) {
      state.SkipWithError("Failed to create process");
    }
  }

  void TearDown(benchmark::State& state) override {
    mx_handle_close(proc);
    mx_handle_close(vmar);
  }

 protected:
  mx_handle_t proc;
  mx_handle_t vmar;
};


BENCHMARK_F(Thread, Create)(benchmark::State& state) {
  mx_handle_t out;
  static const char tname[] = "test thread";
  while (state.KeepRunning()) {
    if (mx_thread_create(proc, tname, sizeof(tname), 0, &out) != MX_OK) {
      state.SkipWithError("Failed to create thread");
      return;
    }
    state.PauseTiming();
    mx_handle_close(out);
    state.ResumeTiming();
  }
}
