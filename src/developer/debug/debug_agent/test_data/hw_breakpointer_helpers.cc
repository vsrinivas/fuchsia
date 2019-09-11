// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "hw_breakpointer_helpers.h"

ThreadSetup::~ThreadSetup() {
  DEFER_PRINT("Joined thread.");

  int res = -1;
  /* FXL_DCHECK(thrd_join(c_thread, &res) == thrd_success); */
  /* FXL_DCHECK(res == 0) << res; */
  thrd_join(c_thread, &res);
}

std::unique_ptr<ThreadSetup> CreateTestSetup(ThreadSetup::Function func, void* user) {
  auto setup = std::make_unique<ThreadSetup>();
  setup->test_running = true;
  setup->user = user;

  CHECK_OK(zx::event::create(0, &setup->event));

  thrd_create(&setup->c_thread, func, setup.get());
  setup->thread.reset(thrd_get_zx_handle(setup->c_thread));

  // We wait until the thread has indicated us we can continue.
  CHECK_OK(setup->event.wait_one(kThreadToHarness, zx::time::infinite(), nullptr));

  return setup;
}

std::pair<zx::port, zx::channel> CreateExceptionChannel(const zx::thread& thread) {
  zx::port port;
  CHECK_OK(zx::port::create(0, &port));

  zx::channel exception_channel;
  CHECK_OK(thread.create_exception_channel(0, &exception_channel));

  return std::make_pair<zx::port, zx::channel>(std::move(port), std::move(exception_channel));
}

zx_thread_state_general_regs_t ReadGeneralRegs(const zx::thread& thread) {
  zx_thread_state_general_regs_t regs = {};
  CHECK_OK(thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
  return regs;
}

void WriteGeneralRegs(const zx::thread& thread, const zx_thread_state_debug_regs_t& regs) {
  CHECK_OK(thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
}

zx_port_packet_t WaitOnPort(const zx::port& port, zx_signals_t signals) {
  // Wait till we get the HW exception.
  zx_port_packet_t packet;
  CHECK_OK(port.wait(zx::time::infinite(), &packet));

  FXL_DCHECK(packet.key == kPortKey);
  FXL_DCHECK(packet.type == ZX_PKT_TYPE_SIGNAL_ONE);
  FXL_DCHECK((packet.signal.observed & signals) != 0);

  return packet;
}

Exception GetException(const zx::channel& exception_channel) {
  Exception exception = {};

  // Obtain the exception.
  CHECK_OK(exception_channel.read(0, &exception.info, exception.handle.reset_and_get_address(),
                                  sizeof(exception.info), 1, nullptr, nullptr));

  /* exception.handle.get_process(&exception.process); */
  CHECK_OK(exception.handle.get_thread(&exception.thread));

  exception.regs = ReadGeneralRegs(exception.thread);

#if defined(__x86_64__)
  exception.pc = exception.regs.rip;
#elif defined(__aarch64__)
  exception.pc = exception.regs.pc;
#else
#error Undefined arch.
#endif

  return exception;
}

Exception WaitForException(const zx::port& port, const zx::channel& exception_channel) {
  PRINT("Waiting for exception.");
  WaitOnPort(port, ZX_CHANNEL_READABLE);
  return GetException(exception_channel);
}

void ResumeException(const zx::thread& thread, Exception&& exception, bool handled) {
  DEFER_PRINT("Resumed from exception. Handled: %d", handled);
  /* #if defined(__aarch64__) */
  /*   // Skip past the brk instruction. Otherwise the breakpoint will trigger again. */
  /*   auto regs = ReadGeneralRegs(thread); */
  /*   regs.pc += 4; */
  /*   WriteGeneralRegs(thread, regs); */
  /* #endif */

  if (handled) {
    uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
    CHECK_OK(exception.handle.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
  }

  exception.handle.reset();
}


void WaitAsyncOnExceptionChannel(const zx::port& port, const zx::channel& exception_channel) {
  // Listen on the exception channel for the thread.

  // Wait on the exception channel.
  CHECK_OK(exception_channel.wait_async(port, kPortKey, ZX_CHANNEL_READABLE, 0));
}

bool IsOnException(const zx::thread& thread) {
  // Get the thread info.
  zx_info_thread_t thread_info;
  CHECK_OK(thread.get_info(ZX_INFO_THREAD, &thread_info, sizeof(thread_info), nullptr, nullptr));

  return thread_info.state == ZX_THREAD_STATE_BLOCKED_EXCEPTION;
}

zx::suspend_token Suspend(const zx::thread& thread) {
  DEFER_PRINT("Suspended thread.");

  // Check if the thread is on an exception.
  if (IsOnException(thread))
    return {};

  // Suspend the thread.
  zx::suspend_token suspend_token;
  CHECK_OK(thread.suspend(&suspend_token));
  CHECK_OK(thread.wait_one(ZX_THREAD_SUSPENDED, zx::time::infinite(), nullptr));

  return suspend_token;
}

// Install HW Breakpoint ---------------------------------------------------------------------------

namespace {

#if defined(__x86_64__)

zx_thread_state_debug_regs_t HWBreakpointRegs(uint64_t address) {
  if (address == 0)
    return {};

  zx_thread_state_debug_regs_t debug_regs = {};
  debug_regs.dr7 = 0b1;
  debug_regs.dr[0] = address;
  return debug_regs;
}

#elif defined(__aarch64__)

zx_thread_state_debug_regs_t HWBreakpointRegs(uint64_t address) {
  if (address == 0)
    return {};

  zx_thread_state_debug_regs_t debug_regs = {};
  auto& hw_bp = debug_regs.hw_bps[0];
  hw_bp.dbgbcr = 1;       // Activate it.
  hw_bp.dbgbvr = address;
  return debug_regs;
}

#else
#error Unsupported arch.
#endif

void SetHWBreakpoint(const zx::thread& thread, uint64_t address) {
  zx::suspend_token suspend_token = Suspend(thread);

  // Install the HW breakpoint.
  auto debug_regs = HWBreakpointRegs(address);
  CHECK_OK(thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs)));

  // Resume the thread.
  suspend_token.reset();
}

}  // namespace

void InstallHWBreakpoint(const zx::thread& thread, uint64_t address) {
  PRINT("Installed hw breakpoint on address 0x%zx", address);
  SetHWBreakpoint(thread, address);
}

void RemoveHWBreakpoint(const zx::thread& thread) {
  PRINT("Removed hw breakpoint.");
  SetHWBreakpoint(thread, 0);
}

// Watchpoint --------------------------------------------------------------------------------------

namespace {

#if defined(__x86_64__)

zx_thread_state_debug_regs_t WatchpointRegs(uint64_t address) {
  zx_thread_state_debug_regs_t debug_regs = {};
  if (address == 0)
    return {};

  debug_regs.dr7 = 0b1 | 1 << 16;
  debug_regs.dr[0] = address;
  return debug_regs;
}

#elif defined(__aarch64__)

zx_thread_state_debug_regs_t WatchpointRegs(uint64_t address) {
  if (address == 0)
    return {};

  zx_thread_state_debug_regs_t debug_regs = {};
  auto& wp = debug_regs.hw_wps[0];
  wp.dbgwcr = 1 | 0b10 << 3 | 1 << 5;
  wp.dbgwvr = address;
  return debug_regs;
}

#else
#error Unsupported arch.
#endif

void SetWatchpoint(const zx::thread& thread, uint64_t address) {
  zx::suspend_token suspend_token = Suspend(thread);

  // Install the HW breakpoint.
  auto debug_regs = WatchpointRegs(address);
  CHECK_OK(thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs)));

  // Resume the thread.
  suspend_token.reset();
}


}  // namespace

void InstallWatchpoint(const zx::thread& thread, uint64_t address) {
  PRINT("Installing one byte watchpoint on 0x%zx", address);
  SetWatchpoint(thread, address);
}

void RemoveWatchpoint(const zx::thread& thread) {
  PRINT("Unintalling watchpoint.");
  SetWatchpoint(thread, 0);
}

