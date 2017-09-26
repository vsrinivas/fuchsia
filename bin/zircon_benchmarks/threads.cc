// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

class Thread : public benchmark::Fixture {};

BENCHMARK_F(Thread, Create)(benchmark::State& state) {
  zx_handle_t out;
  constexpr char tname[] = "test thread";
  while (state.KeepRunning()) {
    if (zx_thread_create(zx_process_self(), tname, sizeof(tname), 0, &out) != ZX_OK) {
      state.SkipWithError("Failed to create thread");
      return;
    }
    state.PauseTiming();
    zx_handle_close(out);
    state.ResumeTiming();
  }
}

BENCHMARK_F(Thread, CreateAndJoin)(benchmark::State& state) {
  zx_handle_t thread;
  constexpr char tname[] = "test thread";
  while (state.KeepRunning()) {
    if (zx_thread_create(zx_process_self(), tname, sizeof(tname), 0, &thread) != ZX_OK) {
      state.SkipWithError("Failed to create thread");
      return;
    }

    uint64_t stack_size = 16 * 1024u;
    zx_handle_t stack_vmo;
    if (zx_vmo_create(stack_size, 0, &stack_vmo) != ZX_OK) {
      state.SkipWithError("Failed to create vmo");
      return;
    }

    zx_vaddr_t stack_base;
    uint32_t perm = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    if (zx_vmar_map(zx_vmar_root_self(), 0, stack_vmo, 0, stack_size, perm, &stack_base) != ZX_OK) {
      state.SkipWithError("Failed to map vmo");
      return;
    }

    uintptr_t entry = reinterpret_cast<uintptr_t>(&zx_thread_exit);
    uintptr_t stack = reinterpret_cast<uintptr_t>(stack_base + stack_size);
    if (zx_thread_start(thread, entry, stack, 0, 0) != ZX_OK) {
      state.SkipWithError("Failed to start thread");
      return;
    }

    zx_signals_t observed;
    if (zx_object_wait_one(thread, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, &observed) != ZX_OK) {
      state.SkipWithError("Failed to wait for thread");
      return;
    }

    state.PauseTiming();
    zx_handle_close(thread);
    zx_handle_close(stack_vmo);
    state.ResumeTiming();
  }
}
