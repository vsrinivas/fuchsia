// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PROFILER_FUCHSIA_THREAD_INTERRUPTER_H_
#define GARNET_LIB_PROFILER_FUCHSIA_THREAD_INTERRUPTER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

class CpuProfiler;

struct InterruptedThreadState {
  uintptr_t pc;
  uintptr_t fp;
};

typedef void (*HandlerCallback)(zx_handle_t thread, void* profiler);

class ThreadInterrupter {
 public:
  static void InitOnce(CpuProfiler* profiler);

  static void Startup();
  static void Shutdown();

  // Delay between interrupts.
  static void SetInterruptPeriod(intptr_t period);

  // Install/uninstall snapshot handler
  static void RegisterHandler(HandlerCallback callback);
  static void UnregisterHandler();

  // Snapshot thread state.
  static void ThreadSnapshot(zx_handle_t thread);

  // Interrupt a thread.
  static void ThreadInterrupt();

  // Grab the key CPU registers for this thread
  static bool GrabRegisters(zx_handle_t thread, InterruptedThreadState* state);

 private:
  static const intptr_t kMaxThreads = 4096;
  static bool initialized_;
  static bool shutdown_;
  static bool thread_running_;
  static bool woken_up_;
  static intptr_t interrupt_period_;
  static intptr_t current_wait_time_;

  static async::Loop* loop_;
  static CpuProfiler* profiler_;
  static HandlerCallback callback_;
};

#endif  // GARNET_LIB_PROFILER_FUCHSIA_THREAD_INTERRUPTER_H_
