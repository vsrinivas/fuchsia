// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_DATA_HW_BREAKPOINTER_HELPERS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_DATA_HW_BREAKPOINTER_HELPERS_H_

#include <lib/fit/defer.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/thread.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/threads.h>

#include <iostream>
#include <mutex>
#include <thread>

#include "src/lib/files/path.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

constexpr uint32_t kHarnessToThread = ZX_USER_SIGNAL_0;
constexpr uint32_t kThreadToHarness = ZX_USER_SIGNAL_1;

// How many ms to wait on an timeout.
constexpr int kExceptionWaitTimeout = 25;

// Thread Test Setup -------------------------------------------------------------------------------

// Control struct for each running test case.
struct ThreadSetup {
  using Function = int (*)(void*);

  ~ThreadSetup();

  zx::event event;
  zx::thread thread;
  thrd_t c_thread;

  std::atomic<bool> test_running = false;
  void* user = nullptr;
};
// |user| is a opaque pointer to specific user data to be passed on to the test.
// Should be stable in memory throughout the test.
std::unique_ptr<ThreadSetup> CreateTestSetup(ThreadSetup::Function func, void* user = nullptr);

// Exception Management ----------------------------------------------------------------------------

struct Exception {
  zx::process process;
  zx::thread thread;

  zx::exception handle;
  zx_exception_info_t info;
  zx_thread_state_general_regs_t regs;
  uint64_t pc = 0;
};
Exception GetException(const zx::channel& exception_channel);
void ResumeException(const zx::thread& thread, Exception&& exception, bool handled = true);

bool IsOnException(const zx::thread& thread);

// Implemented in terms of |WaitOnPort|.
std::optional<Exception> WaitForException(const zx::port& port,
                                          const zx::channel& exception_channel,
                                          zx::time deadline = zx::time::infinite());

std::pair<zx::port, zx::channel> CreateExceptionChannel(const zx::thread&, bool debugger = false);

// Makes the |port| listen for events on the |exception_channel|.
void WaitAsyncOnExceptionChannel(const zx::port& port, const zx::channel& exception_channel);
std::optional<zx_port_packet_t> WaitOnPort(const zx::port& port, zx_signals_t signals,
                                           zx::time deadline = zx::time::infinite());

// Exception Decoding ------------------------------------------------------------------------------

enum class HWExceptionType {
  kSingleStep,
  kHardware,
  kWatchpoint,
  kNone,
};
HWExceptionType DecodeHWException(const zx::thread&, const Exception&);

// Thread Management -------------------------------------------------------------------------------

zx_thread_state_general_regs_t ReadGeneralRegs(const zx::thread& thread);

void WriteGeneralRegs(const zx::thread& thread, const zx_thread_state_general_regs_t& regs);

zx_thread_state_debug_regs_t ReadDebugRegs(const zx::thread&);

// NOTE: This might return an invalid (empty) suspend_token.
//       If that happens, it means that |thread| is on an exception.
zx::suspend_token Suspend(const zx::thread& thread);

void InstallHWBreakpoint(const zx::thread& thread, uint64_t address);
void RemoveHWBreakpoint(const zx::thread& thread);

enum class WatchpointType {
  kWrite,
  kReadWrite,
};

// Length is how many bytes to hit.
// Must be a power of 2 (1, 2, 4, 8 bytes).
void InstallWatchpoint(const zx::thread& thread, uint64_t address, uint32_t length,
                       WatchpointType type = WatchpointType::kWrite);
void RemoveWatchpoint(const zx::thread& thread);

// |exception| means that the thread might be currently on an exception that needs to be resumed.
std::optional<Exception> SingleStep(const zx::thread&, const zx::port&,
                                    const zx::channel& exception_channel,
                                    std::optional<Exception> exception = {});

// Multi-Process Utilities -------------------------------------------------------------------------
//
struct Process {
  std::string name;
  zx::process handle;
  zx::channel comm_channel;
};

// |argv[0]| should have a path to the ELF binary.
zx_status_t LaunchProcess(const zx::job&, const std::string& name,
                          const std::vector<std::string>& argv, Process*);

// Initialization code for a process launched via |LaunchProcess|. Should be call at the beginning
// of the program. Meant to receive coordination resources such as an special channel.
zx_status_t InitSubProcess(zx::channel*);

// Waits on ZX_FIFO_READABLE.
zx_status_t WaitOnChannelReadable(const zx::channel&, zx::time deadline = zx::time::infinite());

constexpr uint32_t kServerToClient = ZX_USER_SIGNAL_0;
constexpr uint32_t kClientToServer = ZX_USER_SIGNAL_1;

zx_status_t SignalClient(const zx::eventpair& event);
zx_status_t SignalServer(const zx::eventpair& event);

// Counterpart call of SignalClient/Server.
zx_status_t WaitForClient(const zx::eventpair& event, zx::time deadline = zx::time::infinite());
zx_status_t WaitForServer(const zx::eventpair& event, zx::time deadline = zx::time::infinite());

// Utilitity Macros --------------------------------------------------------------------------------

#define PRINT_CLEAN(...) \
  { std::cout << fxl::StringPrintf(__VA_ARGS__) << std::endl << std::flush; }

#define PRINT(...)                                                                           \
  {                                                                                          \
    std::cout << "[" << __FILE__ << ":" << __LINE__ << "][t: " << std::this_thread::get_id() \
              << "] " << fxl::StringPrintf(__VA_ARGS__) << std::endl                         \
              << std::flush;                                                                 \
  }

#define DEFER_PRINT(...) auto __defer = fit::defer([=]() { PRINT(__VA_ARGS__); });

#define CHECK_OK(stmt)                                         \
  {                                                            \
    zx_status_t __res = (stmt);                                \
    FXL_DCHECK(__res == ZX_OK) << zx_status_get_string(__res); \
  }

#define ARRAY_SIZE(a) (sizeof((a)) / sizeof((a))[0])

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_DATA_HW_BREAKPOINTER_HELPERS_H_
