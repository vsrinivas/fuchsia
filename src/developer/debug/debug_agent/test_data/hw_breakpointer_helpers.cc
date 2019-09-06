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

zx_thread_state_debug_regs_t ReadGeneralRegs(const zx::thread& thread) {
  zx_thread_state_debug_regs_t regs = {};
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
  // Obtain the exception.
  zx::exception exception;
  zx_exception_info_t info;
  CHECK_OK(exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                  nullptr, nullptr));

  return {std::move(exception), info};
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

std::pair<zx::port, zx::channel> WaitAsyncOnExceptionChannel(const zx::thread& thread) {
  // Listen on the exception channel for the thread.
  zx::channel exception_channel;
  CHECK_OK(thread.create_exception_channel(0, &exception_channel));

  zx::port port;
  CHECK_OK(zx::port::create(0, &port));

  // Wait on the exception channel.
  CHECK_OK(exception_channel.wait_async(port, kPortKey, ZX_CHANNEL_READABLE, 0));

  return {std::move(port), std::move(exception_channel)};
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

zx_thread_state_debug_regs_t GetDebugRegs(uint64_t address) {
  if (address == 0)
    return {};

  PRINT("Setting HW breakpoint to 0x%zx", address);
  zx_thread_state_debug_regs_t debug_regs = {};
  debug_regs.dr7 = 0b1;
  debug_regs.dr[0] = reinterpret_cast<uint64_t>(address);
  return debug_regs;
}

#elif defined(__aarch64__)

zx_thread_state_debug_regs_t GetDebugRegs(uint64_t address) {
  if (address == 0)
    return {};

  zx_thread_state_debug_regs_t debug_regs = {};
  auto& hw_bp = debug_regs.hw_bps[0];
  hw_bp.dbgbcr = 1;  // Activate it.
  hw_bp.dbgbvr = reinterpret_cast<uint64_t>(address);
  return debug_regs;
}

#else
#error Unsupported arch.
#endif

}  // namespace

void InstallHWBreakpoint(const zx::thread& thread, uint64_t address) {
  zx::suspend_token suspend_token = Suspend(thread);

  // Install the HW breakpoint.
  auto debug_regs = GetDebugRegs(address);
  CHECK_OK(thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs)));

  // Resume the thread.
  suspend_token.reset();
}
