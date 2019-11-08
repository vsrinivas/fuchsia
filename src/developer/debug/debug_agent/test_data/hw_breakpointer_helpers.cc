// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "hw_breakpointer_helpers.h"

#if defined(__x86_64__)
#elif defined(__aarch64__)
#include <zircon/hw/debug/arm64.h>
#endif

#include <vector>

ThreadSetup::~ThreadSetup() {
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

std::optional<zx_port_packet_t> WaitOnPort(const zx::port& port, zx_signals_t signals,
                                           zx::time deadline) {
  // Wait till we get the HW exception.
  zx_port_packet_t packet;
  zx_status_t status = port.wait(deadline, &packet);
  if (status == ZX_ERR_TIMED_OUT)
    return std::nullopt;
  CHECK_OK(status);

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

std::optional<Exception> WaitForException(const zx::port& port,
                                          const zx::channel& exception_channel, zx::time deadline) {
  auto packet = WaitOnPort(port, ZX_CHANNEL_READABLE, deadline);
  if (!packet)
    return std::nullopt;
  return GetException(exception_channel);
}

void ResumeException(const zx::thread& thread, Exception&& exception, bool handled) {
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
  hw_bp.dbgbcr = 1;  // Activate it.
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

zx_thread_state_debug_regs_t WatchpointRegs(uint64_t address, uint32_t bytes_to_hit) {
  (void)bytes_to_hit;
  zx_thread_state_debug_regs_t debug_regs = {};
  if (address == 0)
    return {};

  debug_regs.dr7 = 0b1 | 1 << 16;
  debug_regs.dr[0] = address;
  return debug_regs;
}

void arm64_print_debug_registers(const zx_thread_state_debug_regs_t& debug_state) {}

#elif defined(__aarch64__)

zx_thread_state_debug_regs_t WatchpointRegs(uint64_t address, uint32_t length) {
  if (address == 0)
    return {};

  zx_thread_state_debug_regs_t debug_regs = {};
  auto* wp = debug_regs.hw_wps + 0;

  // The instruction has to be 4 byte aligned.
  uint64_t aligned_address = address & (uint64_t)(~0b111);
  uint64_t diff = address - aligned_address;
  FXL_DCHECK(diff <= 7);

  // Set the BAS value.
  uint8_t bas = 0;
  uint8_t extra_bas = 0;
  for (uint32_t i = 0; i < length; i++) {
    uint32_t index = i + diff;

    // We cannot go the beyond the BAS boundary.
    if (index > 7) {
      extra_bas |= (1 << (index - 8));
      continue;
    }

    bas |= (1 << index);
  }

  wp->dbgwvr = aligned_address;

  ARM64_DBGWCR_E_SET(&wp->dbgwcr, 1);
  ARM64_DBGWCR_LSC_SET(&wp->dbgwcr, 0b10);
  ARM64_DBGWCR_BAS_SET(&wp->dbgwcr, bas);

  if (extra_bas) {
    wp = debug_regs.hw_wps + 1;
    uint64_t extra_address = aligned_address + 8;
    wp->dbgwvr = extra_address;

    ARM64_DBGWCR_E_SET(&wp->dbgwcr, 1);
    ARM64_DBGWCR_LSC_SET(&wp->dbgwcr, 0b10);
    ARM64_DBGWCR_BAS_SET(&wp->dbgwcr, extra_bas);
  }

  return debug_regs;
}

// Debug only.
void arm64_print_debug_registers(const zx_thread_state_debug_regs_t& debug_state) {
  printf("HW breakpoints:\n");
  for (uint32_t i = 0; i < ARM64_MAX_HW_BREAKPOINTS; i++) {
    uint32_t dbgbcr = debug_state.hw_bps[i].dbgbcr;
    uint64_t dbgbvr = debug_state.hw_bps[i].dbgbvr;

    if (!ARM64_DBGBCR_E_GET(dbgbcr))
      continue;

    printf(
        "%02u. DBGBVR: 0x%lx, "
        "DBGBCR: E=%d, PMC=%d, BAS=%d, HMC=%d, SSC=%d, LBN=%d, BT=%d\n",
        i, dbgbvr, (int)(dbgbcr & ARM64_DBGBCR_E),
        (int)((dbgbcr & ARM64_DBGBCR_PMC_MASK) >> ARM64_DBGBCR_PMC_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_BAS_MASK) >> ARM64_DBGBCR_BAS_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_HMC_MASK) >> ARM64_DBGBCR_HMC_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_SSC_MASK) >> ARM64_DBGBCR_SSC_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_LBN_MASK) >> ARM64_DBGBCR_LBN_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_BT_MASK) >> ARM64_DBGBCR_BT_SHIFT));
  }

  printf("HW watchpoints:\n");
  for (uint32_t i = 0; i < ARM64_MAX_HW_WATCHPOINTS; i++) {
    uint32_t dbgwcr = debug_state.hw_wps[i].dbgwcr;
    uint64_t dbgwvr = debug_state.hw_wps[i].dbgwvr;

    if (!ARM64_DBGWCR_E_GET(dbgwcr))
      continue;

    printf(
        "%02u. DBGWVR: 0x%lx, DBGWCR: "
        "E=%d, PAC=%d, LSC=%d, BAS=0x%x, HMC=%d, SSC=%d, LBN=%d, WT=%d, MASK=0x%x\n",
        i, dbgwvr, (int)(dbgwcr & ARM64_DBGWCR_E_MASK),
        (int)((dbgwcr & ARM64_DBGWCR_PAC_MASK) >> ARM64_DBGWCR_PAC_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_LSC_MASK) >> ARM64_DBGWCR_LSC_SHIFT),
        (unsigned int)((dbgwcr & ARM64_DBGWCR_BAS_MASK) >> ARM64_DBGWCR_BAS_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_HMC_MASK) >> ARM64_DBGWCR_HMC_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_SSC_MASK) >> ARM64_DBGWCR_SSC_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_LBN_MASK) >> ARM64_DBGWCR_LBN_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_WT_MASK) >> ARM64_DBGWCR_WT_SHIFT),
        (unsigned int)((dbgwcr & ARM64_DBGWCR_MSK_MASK) >> ARM64_DBGWCR_MSK_SHIFT));
  }
}

#else
#error Unsupported arch.
#endif

void SetWatchpoint(const zx::thread& thread, uint64_t address, uint32_t bytes_to_hit) {
  zx::suspend_token suspend_token = Suspend(thread);

  // Install the HW breakpoint.
  auto debug_regs = WatchpointRegs(address, bytes_to_hit);
  static bool a = false;

  if (a) {
    printf("-----------------------------------------------------------\n");
    arm64_print_debug_registers(debug_regs);
    printf("-----------------------------------------------------------\n");
  }

  CHECK_OK(thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs)));

  // Resume the thread.
  suspend_token.reset();
}

}  // namespace

void InstallWatchpoint(const zx::thread& thread, uint64_t address, uint32_t bytes_to_hit) {
  /* PRINT("Installing one byte watchpoint on 0x%zx. Bytes to hit: %s", address, */
  /*       BytesToHitStr(bytes_to_hit).c_str()); */
  SetWatchpoint(thread, address, bytes_to_hit);
}

void RemoveWatchpoint(const zx::thread& thread) {
  /* PRINT("Unintalling watchpoint."); */
  SetWatchpoint(thread, 0, 0);
}
