// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "test_suite_helpers.h"

#if defined(__x86_64__)
#include <zircon/hw/debug/x86.h>
#elif defined(__aarch64__)
#include <zircon/hw/debug/arm64.h>
#endif

#include <lib/fdio/spawn.h>
#include <lib/zx/eventpair.h>
#include <unistd.h>
#include <zircon/exception.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <vector>

constexpr uint64_t kPortKey = 0x11232141234;

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

std::pair<zx::port, zx::channel> CreateExceptionChannel(const zx::thread& thread, bool debugger) {
  zx::port port;
  CHECK_OK(zx::port::create(0, &port));

  zx::channel exception_channel;
  uint32_t flags = debugger ? ZX_EXCEPTION_CHANNEL_DEBUGGER : 0;
  CHECK_OK(thread.create_exception_channel(flags, &exception_channel));

  return std::make_pair<zx::port, zx::channel>(std::move(port), std::move(exception_channel));
}

zx_thread_state_general_regs_t ReadGeneralRegs(const zx::thread& thread) {
  zx_thread_state_general_regs_t regs = {};
  CHECK_OK(thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
  return regs;
}

void WriteGeneralRegs(const zx::thread& thread, const zx_thread_state_general_regs_t& regs) {
  CHECK_OK(thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
}

zx_thread_state_debug_regs_t ReadDebugRegs(const zx::thread& thread) {
  zx_thread_state_debug_regs_t regs = {};
  CHECK_OK(thread.read_state(ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(regs)));
  return regs;
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

HWExceptionType DecodeHWException(const zx::thread& thread, const Exception& exception) {
  if (exception.info.type != ZX_EXCP_HW_BREAKPOINT)
    return HWExceptionType::kNone;

#if defined(__x86_64__)

  // TODO: Implement x64 side logic for this.
  return HWExceptionType::kNone;

#elif defined(__aarch64__)
  auto debug_regs = ReadDebugRegs(thread);

  // The ESR register holds information about the last exception in the form of:
  // |31      26|25|24                              0|
  // |    EC    |IL|             ISS                 |
  //
  // Where:
  // - EC: Exception class field (what exception occurred).
  // - IL: Instruction length (whether the trap was 16-bit of 32-bit instruction).
  // - ISS: Instruction Specific Syndrome. The value is specific to each EC.
  uint32_t ec = debug_regs.esr >> 26;

  switch (ec) {
    case 0b110000: /* HW breakpoint from a lower level */
    case 0b110001: /* HW breakpoint from same level */
      return HWExceptionType::kHardware;
    case 0b110010: /* software step from lower level */
    case 0b110011: /* software step from same level */
      return HWExceptionType::kSingleStep;
    case 0b110100: /* HW watchpoint from a lower level */
    case 0b110101: /* HW watchpoint from same level */
      return HWExceptionType::kWatchpoint;
    default:
      return HWExceptionType::kNone;
  }

#else
#error Undefined arch.
#endif

  return HWExceptionType::kNone;
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
  SetHWBreakpoint(thread, address);
}

void RemoveHWBreakpoint(const zx::thread& thread) { SetHWBreakpoint(thread, 0); }

// Watchpoint --------------------------------------------------------------------------------------

namespace {

#if defined(__x86_64__)

#define SET_REG(num, reg, len, address, type)     \
  {                                               \
    X86_DBG_CONTROL_L##num##_SET((reg), 1);       \
    X86_DBG_CONTROL_RW##num##_SET((reg), (type)); \
    X86_DBG_CONTROL_LEN##num##_SET((reg), (len)); \
    regs.dr[(num)] = (address);                   \
  }

#define BYTES_1 0
#define BYTES_2 1
#define BYTES_4 3
#define BYTES_8 2

zx_thread_state_debug_regs_t WatchpointRegs(uint64_t address, uint32_t length,
                                            WatchpointType type) {
  zx_thread_state_debug_regs_t regs = {};
  if (address == 0)
    return {};

  uint64_t align_mask = 0;
  switch (length) {
    case 1:
      break;
    case 2:
      align_mask = static_cast<uint64_t>(~0b1);
      break;
    case 4:
      align_mask = static_cast<uint64_t>(~0b11);
      break;
    case 8:
      align_mask = static_cast<uint64_t>(~0b111);
      break;
    default:
      FXL_NOTREACHED() << "Invalid length: " << length;
      break;
  }

  uint32_t type_val = (type == WatchpointType::kWrite) ? 0b01 : 0b11;

  if (length == 1) {
    SET_REG(0, &regs.dr7, BYTES_1, address, type_val);
  } else if (length == 2) {
    uint64_t aligned_address = address & static_cast<uint64_t>(~0b1);
    uint64_t diff = address - aligned_address;

    if (!diff) {
      SET_REG(0, &regs.dr7, BYTES_2, address, type_val);
    } else {
      SET_REG(0, &regs.dr7, BYTES_1, address, type_val);
      SET_REG(1, &regs.dr7, BYTES_1, address + 1, type_val);
    }
  } else if (length == 4) {
    uint64_t aligned_address = address & static_cast<uint64_t>(~0b11);
    uint64_t diff = address - aligned_address;

    switch (diff) {
      case 0:
        SET_REG(0, &regs.dr7, BYTES_4, address, type_val);
        break;
      case 1:
      case 3:
        SET_REG(0, &regs.dr7, BYTES_1, address, type_val);
        SET_REG(1, &regs.dr7, BYTES_2, address + 1, type_val);
        SET_REG(2, &regs.dr7, BYTES_1, address + 3, type_val);
        break;
      case 2:
        SET_REG(0, &regs.dr7, BYTES_2, address, type_val);
        SET_REG(1, &regs.dr7, BYTES_2, address + 2, type_val);
        break;
      default:
        FXL_NOTREACHED() << "Invalid diff: " << diff;
        break;
    }
  } else if (length == 8) {
    uint64_t aligned_address = address & static_cast<uint64_t>(~0b111);
    uint64_t diff = address - aligned_address;

    /* FXL_LOG(INFO) << "Diff: " << diff; */

    switch (diff) {
      case 0:
        SET_REG(0, &regs.dr7, BYTES_8, address, type_val);
        break;
      case 1:
      case 5:
        SET_REG(0, &regs.dr7, BYTES_1, address, type_val);
        SET_REG(1, &regs.dr7, BYTES_2, address + 1, type_val);
        SET_REG(2, &regs.dr7, BYTES_4, address + 3, type_val);
        SET_REG(3, &regs.dr7, BYTES_1, address + 7, type_val);
        break;

      case 2:
      case 6:
        SET_REG(0, &regs.dr7, BYTES_2, address, type_val);
        SET_REG(1, &regs.dr7, BYTES_4, address + 2, type_val);
        SET_REG(2, &regs.dr7, BYTES_2, address + 6, type_val);
        break;
      case 3:
      case 7:
        SET_REG(0, &regs.dr7, BYTES_1, address, type_val);
        SET_REG(1, &regs.dr7, BYTES_4, address + 1, type_val);
        SET_REG(2, &regs.dr7, BYTES_2, address + 5, type_val);
        SET_REG(3, &regs.dr7, BYTES_1, address + 7, type_val);
        break;
      case 4:
        SET_REG(0, &regs.dr7, BYTES_4, address, type_val);
        SET_REG(1, &regs.dr7, BYTES_4, address + 4, type_val);
        break;
      default:
        FXL_NOTREACHED() << "Invalid diff: " << diff;
        break;
    }

  } else {
    FXL_NOTREACHED() << "Invalid length: " << length;
  }

  return regs;
}

void PrintDebugRegs(const zx_thread_state_debug_regs_t& debug_state) {
  // DR6
  printf("DR6: 0x%lx -> B0=%lu, B1=%lu, B2=%lu, B3=%lu, BD=%lu, BS=%lu, BT=%lu\n", debug_state.dr6,
         X86_DBG_STATUS_B0_GET(debug_state.dr6), X86_DBG_STATUS_B1_GET(debug_state.dr6),
         X86_DBG_STATUS_B2_GET(debug_state.dr6), X86_DBG_STATUS_B3_GET(debug_state.dr6),
         X86_DBG_STATUS_BD_GET(debug_state.dr6), X86_DBG_STATUS_BS_GET(debug_state.dr6),
         X86_DBG_STATUS_BT_GET(debug_state.dr6));

  // DR7
  printf(
      "DR7: 0x%lx -> L0=%lu, G0=%lu, L1=%lu, G1=%lu, L2=%lu, G2=%lu, L3=%lu, G4=%lu, LE=%lu, "
      "GE=%lu, GD=%lu\n",
      debug_state.dr7, X86_DBG_CONTROL_L0_GET(debug_state.dr7),
      X86_DBG_CONTROL_G0_GET(debug_state.dr7), X86_DBG_CONTROL_L1_GET(debug_state.dr7),
      X86_DBG_CONTROL_G1_GET(debug_state.dr7), X86_DBG_CONTROL_L2_GET(debug_state.dr7),
      X86_DBG_CONTROL_G2_GET(debug_state.dr7), X86_DBG_CONTROL_L3_GET(debug_state.dr7),
      X86_DBG_CONTROL_G3_GET(debug_state.dr7), X86_DBG_CONTROL_LE_GET(debug_state.dr7),
      X86_DBG_CONTROL_GE_GET(debug_state.dr7), X86_DBG_CONTROL_GD_GET(debug_state.dr7));

  printf("R/W0=%lu, LEN0=%lu, R/W1=%lu, LEN1=%lu, R/W2=%lu, LEN2=%lu, R/W3=%lu, LEN3=%lu\n",
         X86_DBG_CONTROL_RW0_GET(debug_state.dr7), X86_DBG_CONTROL_LEN0_GET(debug_state.dr7),
         X86_DBG_CONTROL_RW1_GET(debug_state.dr7), X86_DBG_CONTROL_LEN1_GET(debug_state.dr7),
         X86_DBG_CONTROL_RW2_GET(debug_state.dr7), X86_DBG_CONTROL_LEN2_GET(debug_state.dr7),
         X86_DBG_CONTROL_RW3_GET(debug_state.dr7), X86_DBG_CONTROL_LEN3_GET(debug_state.dr7));
}

#elif defined(__aarch64__)

zx_thread_state_debug_regs_t WatchpointRegs(uint64_t address, uint32_t length,
                                            WatchpointType type) {
  if (address == 0)
    return {};

  zx_thread_state_debug_regs_t debug_regs = {};
  auto* wp = debug_regs.hw_wps + 0;

  // The instruction has to be 4 byte aligned.
  uint64_t aligned_address = address & static_cast<uint64_t>(~0b111);
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

  uint32_t lsc = (type == WatchpointType::kWrite) ? 0b10 : 0b11;

  ARM64_DBGWCR_E_SET(&wp->dbgwcr, 1);
  ARM64_DBGWCR_LSC_SET(&wp->dbgwcr, lsc);
  ARM64_DBGWCR_BAS_SET(&wp->dbgwcr, bas);

  if (extra_bas) {
    wp = debug_regs.hw_wps + 1;
    uint64_t extra_address = aligned_address + 8;
    wp->dbgwvr = extra_address;

    ARM64_DBGWCR_E_SET(&wp->dbgwcr, 1);
    ARM64_DBGWCR_LSC_SET(&wp->dbgwcr, lsc);
    ARM64_DBGWCR_BAS_SET(&wp->dbgwcr, extra_bas);
  }

  return debug_regs;
}

void PrintDebugRegs(const zx_thread_state_debug_regs_t& debug_state) {
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

void SetWatchpoint(const zx::thread& thread, uint64_t address, uint32_t length,
                   WatchpointType type) {
  zx::suspend_token suspend_token = Suspend(thread);

  // Install the HW breakpoint.
  auto debug_regs = WatchpointRegs(address, length, type);
  static bool a = false;

  if (a) {
    printf("-----------------------------------------------------------\n");
    PrintDebugRegs(debug_regs);
    printf("-----------------------------------------------------------\n");
  }

  CHECK_OK(thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs)));

  // Resume the thread.
  suspend_token.reset();
}

}  // namespace

void InstallWatchpoint(const zx::thread& thread, uint64_t address, uint32_t length,
                       WatchpointType type) {
  SetWatchpoint(thread, address, length, type);
}

void RemoveWatchpoint(const zx::thread& thread) {
  SetWatchpoint(thread, 0, 0, WatchpointType::kWrite);
}

std::optional<Exception> SingleStep(const zx::thread& thread, const zx::port& port,
                                    const zx::channel& exception_channel,
                                    std::optional<Exception> exception) {
  {
    zx_thread_state_single_step_t value = 1;
    zx::suspend_token suspend_token = Suspend(thread);
    CHECK_OK(thread.write_state(ZX_THREAD_STATE_SINGLE_STEP, &value, sizeof(value)));

    WaitAsyncOnExceptionChannel(port, exception_channel);
    ResumeException(thread, std::move(*exception));
  }

  exception = WaitForException(port, exception_channel,
                               zx::deadline_after(zx::msec(kExceptionWaitTimeout)));

  FXL_DCHECK(exception.has_value()) << "No exception!";
  FXL_DCHECK(exception->info.type == ZX_EXCP_HW_BREAKPOINT)
      << "Got: " << zx_exception_get_string(exception->info.type);
  FXL_DCHECK(DecodeHWException(thread, exception.value()) == HWExceptionType::kSingleStep);

  {
    zx_thread_state_single_step_t value = 0;
    zx::suspend_token suspend_token = Suspend(thread);
    CHECK_OK(thread.write_state(ZX_THREAD_STATE_SINGLE_STEP, &value, sizeof(value)));
  }

  return exception;
}

// Process Spawning --------------------------------------------------------------------------------

zx_status_t LaunchProcess(const zx::job& job, const std::string& name,
                          const std::vector<std::string>& argv, Process* out) {
  // fdio_spawn_etc expects argv to be a nullptr-terminated array.
  std::vector<const char*> normalized_argv;
  normalized_argv.reserve(argv.size() + 1);
  for (const std::string& arg : argv) {
    normalized_argv.push_back(arg.c_str());
  }
  normalized_argv.push_back(nullptr);

  zx::channel mine, theirs;
  CHECK_OK(zx::channel::create(0, &mine, &theirs));

  // clang-format off
  fdio_spawn_action_t actions[] = {
    // Set the process name.
    {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = name.c_str()}},
    // Pass in a special channel handle that they can obtain via |zx_take_startup_handle|.
    // See |InitSubProcess|.
    {.action = FDIO_SPAWN_ACTION_ADD_HANDLE, .h = {.id = PA_HND(PA_USER0, 0), .handle = theirs.release()}},
    // Clone stdout/err/in.
    {.action = FDIO_SPAWN_ACTION_CLONE_FD, .fd = {.local_fd = STDOUT_FILENO, .target_fd = STDOUT_FILENO}},
    {.action = FDIO_SPAWN_ACTION_CLONE_FD, .fd = {.local_fd =  STDIN_FILENO, .target_fd =  STDIN_FILENO}},
    {.action = FDIO_SPAWN_ACTION_CLONE_FD, .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}},
  };
  // clang-format on

  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status = fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, normalized_argv.front(),
                                      normalized_argv.data(), nullptr, std::size(actions), actions,
                                      process.reset_and_get_address(), err_msg);

  if (status != ZX_OK)
    return status;

  *out = {};
  out->name = name;
  out->handle = std::move(process);
  out->comm_channel = std::move(mine);

  return ZX_OK;
}

zx_status_t InitSubProcess(zx::channel* out) {
  zx::channel channel{zx_take_startup_handle(PA_HND(PA_USER0, 0))};
  if (!channel.is_valid())
    return ZX_ERR_BAD_STATE;
  *out = std::move(channel);
  return ZX_OK;
}

zx_status_t WaitOnChannelReadable(const zx::channel& channel, zx::time deadline) {
  return channel.wait_one(ZX_FIFO_READABLE, zx::deadline_after(zx::sec(1)), nullptr);
}

zx_status_t SignalClient(const zx::eventpair& event) {
  CHECK_OK(event.signal(kClientToServer, 0));
  return event.signal_peer(0, kServerToClient);
}

zx_status_t SignalServer(const zx::eventpair& event) {
  CHECK_OK(event.signal(kServerToClient, 0));
  return event.signal_peer(0, kClientToServer);
}

zx_status_t WaitForClient(const zx::eventpair& event, zx::time deadline) {
  return event.wait_one(kClientToServer, deadline, 0);
}

zx_status_t WaitForServer(const zx::eventpair& event, zx::time deadline) {
  return event.wait_one(kServerToClient, deadline, 0);
}
