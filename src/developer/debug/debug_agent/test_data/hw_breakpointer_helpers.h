// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#pragma once

#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/port.h>
#include <lib/zx/thread.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/threads.h>

#include <iostream>
#include <lib/fit/defer.h>
#include <thread>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"


#define PRINT(...)                                                                               \
  std::cout << std::this_thread::get_id() << ": " << fxl::StringPrintf(__VA_ARGS__) << std::endl \
            << std::flush;

#define DEFER_PRINT(...) auto __defer = fit::defer([=]() { PRINT(__VA_ARGS__); });

#define CHECK_OK(stmt)                                         \
  {                                                            \
    zx_status_t __res = (stmt);                                \
    FXL_DCHECK(__res == ZX_OK) << zx_status_get_string(__res); \
  }

constexpr char kBeacon[] = "Counter: Thread running.\n";
constexpr int kPortKey = 0x2312451;

constexpr uint32_t kHarnessToThread = ZX_USER_SIGNAL_0;
constexpr uint32_t kThreadToHarness = ZX_USER_SIGNAL_1;

// Control struct for each running test case.
struct ThreadSetup {
  using Function = int(*)(void*);

  ~ThreadSetup();

  zx::event event;
  zx::thread thread;
  thrd_t c_thread;

  std::atomic<bool> test_running = false;
  void* extra_data = nullptr;
};

std::unique_ptr<ThreadSetup> CreateTestSetup(ThreadSetup::Function func);

zx_thread_state_debug_regs_t ReadGeneralRegs(const zx::thread& thread);

void WriteGeneralRegs(const zx::thread& thread, const zx_thread_state_debug_regs_t& regs);

zx_port_packet_t WaitOnPort(const zx::port& port, zx_signals_t signals);

struct Exception {
  zx::exception handle;
  zx_exception_info_t info;
};

Exception GetException(const zx::channel& exception_channel);

Exception WaitForException(const zx::port& port, const zx::channel& exception_channel);

void ResumeException(const zx::thread& thread, Exception&& exception, bool handled = true);

std::pair<zx::port, zx::channel> WaitAsyncOnExceptionChannel(const zx::thread& thread);

bool IsOnException(const zx::thread& thread);

// NOTE: This might return an invalid (empty) suspend_token.
//       If that happens, it means that |thread| is on an exception.
zx::suspend_token Suspend(const zx::thread& thread);

void InstallHWBreakpoint(const zx::thread& thread, uint64_t address);
