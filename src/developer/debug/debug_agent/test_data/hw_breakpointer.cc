// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/eventpair.h>

#include <string>
#include <vector>

#include "hw_breakpointer_helpers.h"

// This is a self contained binary that is meant to be run *manually*. This is the smallest code
// that can be used to reproduce a HW breakpoint exception.
// This is meant to be able to test the functionality of zircon without having to go throught the
// hassle of having the whole debugger context around.

namespace {

// Test Cases ======================================================================================

// BreakOnFunction ---------------------------------------------------------------------------------
//
// 1. Create a thread that will loop forever, continually calling a particular
//    function.
// 2. Suspend that thread.
// 3. Install a HW breakpoint through zx_thread_write_state.
// 4. Resume the thread.
// 5. Wait for some time for the exception. If the exception never happened, it
//    means that Zircon is not doing the right thing.

// This is the function that we will set up the breakpoint to.
using HWBreakpointTestCaseFunctionToBeCalled = int (*)(int);
int __NO_INLINE FunctionToBreakpointOn1(int c) { return c + c; }
int __NO_INLINE FunctionToBreakpointOn2(int c) { return c + c; }
int __NO_INLINE FunctionToBreakpointOn3(int c) { return c + c; }
int __NO_INLINE FunctionToBreakpointOn4(int c) { return c + c; }
int __NO_INLINE FunctionToBreakpointOn5(int c) { return c + c; }

const char kBeacon[] = "Counter: Thread running.\n";

// This is the code that the new thread will run.
// It's meant to be an eternal loop.
int BreakOnFunctionThreadFunction(void* user) {
  auto* thread_setup = reinterpret_cast<ThreadSetup*>(user);

  // We signal the test harness that we are here.
  thread_setup->event.signal(kHarnessToThread, kThreadToHarness);

  // We wait now for the harness to tell us we can continue.
  CHECK_OK(thread_setup->event.wait_one(kHarnessToThread, zx::time::infinite(), nullptr));

  PRINT("Got signaled by harness.");

  int counter = 1;
  while (thread_setup->test_running) {
    auto* function_to_call =
        reinterpret_cast<HWBreakpointTestCaseFunctionToBeCalled>(thread_setup->user);
    FXL_DCHECK(function_to_call);

    // We use write to avoid deadlocking with the outside libc calls.
    write(1, kBeacon, sizeof(kBeacon));
    counter = function_to_call(counter);
    zx::nanosleep(zx::deadline_after(zx::sec(1)));
  }

  return 0;
}

void BreakOnFunctionTestCase() {
  printf("Running HW breakpoint when calling a function test.\n");

  // The functions to be called sequentially by the test.
  // clang-format off
  HWBreakpointTestCaseFunctionToBeCalled breakpoint_functions[] = {
      FunctionToBreakpointOn1,
      FunctionToBreakpointOn2,
      FunctionToBreakpointOn3,
      FunctionToBreakpointOn4,
      FunctionToBreakpointOn5,
  };
  // clang-format on

  auto thread_setup = CreateTestSetup(BreakOnFunctionThreadFunction);
  auto [port, exception_channel] = CreateExceptionChannel(thread_setup->thread);
  WaitAsyncOnExceptionChannel(port, exception_channel);

  Exception exception = {};
  for (size_t i = 0; i < std::size(breakpoint_functions); i++) {
    // If this is the first iteration, we don't resume the exception.
    if (i > 0u) {
      WaitAsyncOnExceptionChannel(port, exception_channel);
      ResumeException(thread_setup->thread, std::move(exception));
    }

    auto* breakpoint_function = breakpoint_functions[i];

    // Pass in the function to call as extra data.
    thread_setup->user = reinterpret_cast<void*>(breakpoint_function);

    // Install the breakpoint.
    uint64_t breakpoint_address = reinterpret_cast<uint64_t>(breakpoint_function);
    InstallHWBreakpoint(thread_setup->thread, breakpoint_address);

    // Tell the thread to continue.
    thread_setup->event.signal(kThreadToHarness, kHarnessToThread);

    // We wait until we receive an exception.
    auto opt_excp = WaitForException(port, exception_channel);
    FXL_DCHECK(opt_excp.has_value());
    exception = std::move(*opt_excp);

    FXL_DCHECK(exception.info.type == ZX_EXCP_HW_BREAKPOINT);
    PRINT("Hit HW breakpoint %zu on 0x%zx", i, exception.pc);

    // Remove the breakpoint.
    RemoveHWBreakpoint(thread_setup->thread);
  }

  // Tell the thread to exit.
  thread_setup->test_running = false;
  ResumeException(thread_setup->thread, std::move(exception));
}

// Watchpoints -------------------------------------------------------------------------------------
//
// This test has an array of bytes that will be accessed one by one by another thread.
// The harness will set a watchpoint on each of those bytes and expects to receive an exception for
// of them.

uint8_t gDataToTouch[16] = {};

int WatchpointThreadFunction(void* user) {
  auto* thread_setup = reinterpret_cast<ThreadSetup*>(user);

  // We signal the test harness that we are here.
  thread_setup->event.signal(kHarnessToThread, kThreadToHarness);

  while (thread_setup->test_running) {
    // We wait now for the harness to tell us we can continue.
    CHECK_OK(thread_setup->event.wait_one(kHarnessToThread, zx::time::infinite(), nullptr));

    uint8_t* byte = reinterpret_cast<uint8_t*>(thread_setup->user);
    FXL_DCHECK(byte);

    *byte += 1;

    // We signal that we finished this write.
    CHECK_OK(thread_setup->event.signal(kHarnessToThread, kThreadToHarness));
  }

  return 0;
}

// Returns whether the breakpoint was hit.
bool TestWatchpointRun(const zx::port& port, const zx::channel& exception_channel,
                       ThreadSetup* thread_setup, uint64_t wp_address, uint32_t length,
                       uint8_t* address_to_write) {
  thread_setup->user = address_to_write;

  // Install the watchpoint.
  InstallWatchpoint(thread_setup->thread, wp_address, length);

  // Tell the thread to continue.
  CHECK_OK(thread_setup->event.signal(kThreadToHarness, kHarnessToThread));

  // Wait until the exception is hit.
  auto opt_excp = WaitForException(port, exception_channel,
                                   zx::deadline_after(zx::msec(kExceptionWaitTimeout)));

  // Remove the watchpoint.
  RemoveWatchpoint(thread_setup->thread);

  if (!opt_excp) {
    PRINT_CLEAN("Writing into 0x%zx.", (uint64_t)address_to_write);
    return false;
  }

  Exception exception = std::move(*opt_excp);

  FXL_DCHECK(exception.info.type = ZX_EXCP_HW_BREAKPOINT);
  PRINT_CLEAN("Writing into 0x%zx. Hit!", (uint64_t)address_to_write);

  WaitAsyncOnExceptionChannel(port, exception_channel);
  ResumeException(thread_setup->thread, std::move(exception));

  // Wait until the thread tells us it's ready.
  CHECK_OK(thread_setup->event.wait_one(kThreadToHarness, zx::time::infinite(), nullptr));

  return true;
}

void WatchpointTestCase() {
  PRINT("Running Watchpoint test case.");

  auto thread_setup = CreateTestSetup(WatchpointThreadFunction);
  auto [port, exception_channel] = CreateExceptionChannel(thread_setup->thread);
  WaitAsyncOnExceptionChannel(port, exception_channel);

  uint32_t kSizes[] = {1, 2, 4, 8};
  for (uint32_t size : kSizes) {
    PRINT_CLEAN("====================================================================");
    PRINT_CLEAN("%u BYTE ALIGNED WATCHPOINTS", size);
    for (size_t i = 0; i < std::size(gDataToTouch); i++) {
      uint64_t brk = reinterpret_cast<uint64_t>(gDataToTouch) + i;

      if (i > 0)
        PRINT_CLEAN("----------------------------------------");
      PRINT_CLEAN("* Setting %u byte watchpoint for 0x%zx\n", size, brk);

      for (size_t j = 0; j < std::size(gDataToTouch); j++) {
        // Pass in the byte to break on.
        uint8_t* data_ptr = gDataToTouch + j;
        bool hit =
            TestWatchpointRun(port, exception_channel, thread_setup.get(), brk, size, data_ptr);

        // We should only hit if it is the expected byte.
        bool in_range = (j - i) < size;
        if (hit) {
          FXL_DCHECK(in_range) << "i: " << i << ", j: " << j << ". Got unexpected hit.";
        } else {
          FXL_DCHECK(!in_range) << "i: " << i << ", j: " << j << ". Didn't get expected hit.";
        }
      }
    }
  }

  // Tell the thread to exit.
  thread_setup->test_running = false;
  CHECK_OK(thread_setup->event.signal(kThreadToHarness, kHarnessToThread));
}

// Aligned Watchpoint ------------------------------------------------------------------------------
//
// This test runs a thread that within a loop prints a group of ints, increments them (via var++)
// and then prints it again (the function is AlignedWatchpointThreadFunction).
// On the control thread, it sets a read/write watchpoint on one of the globals and verifies that
// the following accesses are hit:
//
// 1. Read on the first printf.
// 2. Read on the var++.
// 3. Write on the var++.
// 4. Read on the second printf.
//
// NOTE: In order to do this correctly, this tests does the same thing that zxdb does when it
//       encounters a breakpoint: It deactivates the breakpoint, single steps the thread and then
//       installs the breakpoint again. The watchpoint here is installed/uninstalled for every hit
//       and the thread is single stepped.

int SomeInt = 10;
int SomeInt2 = 20;
int SomeInt3 = 30;
int SomeInt4 = 40;

struct AlignedWatchpointUserData {
  // How many times to run the test.
  int times = 10;
};

int AlignedWatchpointThreadFunction(void* user) {
  auto* thread_setup = reinterpret_cast<ThreadSetup*>(user);
  auto* user_data = reinterpret_cast<AlignedWatchpointUserData*>(thread_setup->user);

  // We signal the test harness that we are here.
  thread_setup->event.signal(kHarnessToThread, kThreadToHarness);

  CHECK_OK(thread_setup->event.wait_one(kHarnessToThread, zx::time::infinite(), nullptr));

  printf("User data times: %d.\n", user_data->times);
  for (int i = 0; i < user_data->times; i++) {
    printf("Before: %d, %d, %d, %d\n", SomeInt, SomeInt2, SomeInt3, SomeInt4);
    SomeInt++;
    SomeInt2++;
    SomeInt3++;
    SomeInt4++;
    printf("After:  %d, %d, %d, %d\n", SomeInt, SomeInt2, SomeInt3, SomeInt4);
    printf("-----------------------------\n");
  }

  fflush(stdout);

  // We signal that we finished this write.
  CHECK_OK(thread_setup->event.signal(kHarnessToThread, kThreadToHarness));

  return 0;
}

void WatchpointStepOver(uint64_t wp_address, const zx::thread& thread, const zx::port& port,
                        const zx::channel& exception_channel, std::optional<Exception> exception) {
  RemoveWatchpoint(thread);

  exception = SingleStep(thread, port, exception_channel, std::move(exception));
  FXL_DCHECK(exception);

  // Now that we have single stepped, we can reinstall the watchpoint.
  InstallWatchpoint(thread, wp_address, 4, WatchpointType::kReadWrite);

  WaitAsyncOnExceptionChannel(port, exception_channel);
  ResumeException(thread, std::move(*exception));
}

#define GET_DEADLINE(timeout) zx::deadline_after(zx::msec((timeout)))

void AlignedWatchpointTestCase() {
  PRINT("Running aligned watchpoint test case.");
  PRINT("SomeInt:  0x%p", &SomeInt);
  PRINT("SomeInt2: 0x%p", &SomeInt2);
  PRINT("SomeInt3: 0x%p", &SomeInt3);
  PRINT("SomeInt4: 0x%p", &SomeInt4);

  // Create test setup.
  AlignedWatchpointUserData user_data = {};
  user_data.times = 1;
  auto thread_setup = CreateTestSetup(AlignedWatchpointThreadFunction, &user_data);
  const zx::thread& thread = thread_setup->thread;

  auto [port, exception_channel] = CreateExceptionChannel(thread);
  WaitAsyncOnExceptionChannel(port, exception_channel);

  // We install a watchpoint.
  uint64_t wp_address = reinterpret_cast<uint64_t>(&SomeInt);
  InstallWatchpoint(thread, wp_address, 4, WatchpointType::kReadWrite);

  // Tell the test to run.
  CHECK_OK(thread_setup->event.signal(kThreadToHarness, kHarnessToThread));

  for (int i = 0; i < user_data.times; i++) {
    uint64_t pc = 0;
    PRINT("ITERATION %d ---------------------------------------------------------", i);
    // Wait until the exception is hit.
    auto exception = WaitForException(port, exception_channel, GET_DEADLINE(kExceptionWaitTimeout));
    FXL_DCHECK(exception.has_value());
    FXL_DCHECK(exception->pc > pc);
    FXL_DCHECK(DecodeHWException(thread, exception.value()) == HWExceptionType::kWatchpoint);
    pc = exception->pc;
    PRINT("Exception on 0x%p: Hit first printf read!", (void*)exception->pc);

    WatchpointStepOver(wp_address, thread, port, exception_channel, std::move(exception));

    exception = WaitForException(port, exception_channel, GET_DEADLINE(kExceptionWaitTimeout));
    FXL_DCHECK(exception.has_value());
    FXL_DCHECK(exception->pc > pc);
    FXL_DCHECK(DecodeHWException(thread, exception.value()) == HWExceptionType::kWatchpoint);
    pc = exception->pc;
    PRINT("Exception on 0x%p: Hit ++ read!", (void*)exception->pc);

    WatchpointStepOver(wp_address, thread, port, exception_channel, std::move(exception));

    exception = WaitForException(port, exception_channel, GET_DEADLINE(kExceptionWaitTimeout));
    FXL_DCHECK(exception.has_value());
    FXL_DCHECK(exception->pc > pc);
    FXL_DCHECK(DecodeHWException(thread, exception.value()) == HWExceptionType::kWatchpoint);
    pc = exception->pc;
    PRINT("Exception on 0x%p: Hit ++ write!", (void*)exception->pc);

    WatchpointStepOver(wp_address, thread, port, exception_channel, std::move(exception));

    exception = WaitForException(port, exception_channel, GET_DEADLINE(kExceptionWaitTimeout));
    FXL_DCHECK(exception.has_value());
    FXL_DCHECK(exception->pc > pc);
    FXL_DCHECK(DecodeHWException(thread, exception.value()) == HWExceptionType::kWatchpoint);
    pc = exception->pc;
    PRINT("Exception on 0x%p: Hit second printf read!", (void*)exception->pc);

    WatchpointStepOver(wp_address, thread, port, exception_channel, std::move(exception));
  }

  // Wait until the thread is done.
  CHECK_OK(thread_setup->event.wait_one(kThreadToHarness, zx::time::infinite(), nullptr));
}

// Channel messaging -------------------------------------------------------------------------------
//
// 1. Thread writes a set of messages into the channel then closes its endpoint.
// 2. The main thread will wait until the channel has been closed.
// 3. It will then read all the messages from it.

int ChannelMessagingThreadFunction(void* user) {
  ThreadSetup* thread_setup = reinterpret_cast<ThreadSetup*>(user);

  // We signal the test harness that we are here.
  thread_setup->event.signal(kHarnessToThread, kThreadToHarness);

  // We wait now for the harness to tell us we can continue.
  CHECK_OK(thread_setup->event.wait_one(kHarnessToThread, zx::time::infinite(), nullptr));

  zx::channel* channel = reinterpret_cast<zx::channel*>(thread_setup->user);

  constexpr char kMsg[] = "Hello, World!";

  for (int i = 0; i < 10; i++) {
    CHECK_OK(channel->write(0, kMsg, sizeof(kMsg), nullptr, 0));
    PRINT("Added message %d.", i);
  }

  channel->reset();
  PRINT("Closed channel.");

  return 0;
}

void ChannelMessagingTestCase() {
  PRINT("Running channel messaging.");

  zx::channel mine, theirs;
  CHECK_OK(zx::channel::create(0, &mine, &theirs));

  auto thread_setup = CreateTestSetup(ChannelMessagingThreadFunction, &theirs);

  // Tell the thread to continue.
  thread_setup->event.signal(kThreadToHarness, kHarnessToThread);

  // Wait for peer closed.
  CHECK_OK(mine.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));

  // Start reading from the channel.
  int read_count = 0;
  char buf[1024] = {};
  while (true) {
    zx_status_t status = mine.read_etc(0, buf, nullptr, sizeof(buf), 0, nullptr, nullptr);
    if (status == ZX_OK) {
      PRINT("Read message %d: %s", read_count++, buf);
    } else {
      PRINT("No more messages (status: %s).", zx_status_get_string(status));
      break;
    }
  }

  thread_setup->test_running = false;
}

// Watchpoint Server/Client ------------------------------------------------------------------------
//
// The test spawns a new process with this binary, but passing the |watchpoint_client| option.
// It coordinates through a channel and an event. The idea is that the server listen on the debugger
// exception port of the client and setup a read-write watchpoint on a thread of the client.
// The client runs the same thread as AlignedWatchpointTestCase (AlignedWatchpointThreadFunction),
// which the server will set a brekapoint to. Basically it's a multi-process |aligned_watchpoint|
// test.
//
// The setup is as follows:
// 1. Client sends the addresses of the ints (SomeInt, SomeInt2, etc.). It also passes the memory
//    associated with AlignedWatchpointThreadFunction, so that the server can verify which address
//    actually triggered the exception.
// 2. The server listens on the exception port of the client and sets up a R/W Breakpoint.
// 3. The client starts another thread with AlignedWatchpointThreadFunction.
// 4. The server verifies that all the expected watchpoint exceptions are hit.

#define DEADLINE(d) zx::deadline_after(zx::sec((d)))

constexpr uint32_t kIterations = 1000;

constexpr uint32_t kInstructionBufferSize = 4096;

// These are the instructions that the server expects the client to have in the PC when it triggers
// each of the exceptions. Thhe client will send |kInstructionBufferSize| instructions starting
// with the first of |AlignedWatchpointThreadFunction| and the base address of it. That way the
// server can see the offset and see which instruction triggered the exception.
constexpr uint32_t kPrintLoad1 = 0xb9400101;  // ldr w1, [x8]
constexpr uint32_t kPlusRead = 0xb940010c;    // ldr w12, [x8]
constexpr uint32_t kPlusWrite = 0xb900010c;   // str w12, [x8]
constexpr uint32_t kPrintLoad2 = 0xb9400101;  // ldr w1, [x8]

// Verifies the exception that was triggered by the client.
// |instructions| is an array with the instructions of |AlignedWatchpointThreadFunction.
// |base_address| is the address where |AlignedWatchpointThreadFunction| starts.
// |expected_instruction| is what we expect the pc points to.
uint64_t CheckWatchpointException(const std::optional<Exception>& exception,
                                  const zx::thread& thread, const zx::port& port,
                                  const zx::channel& exception_channel, uint32_t* instructions,
                                  uint32_t expected_instruction, uint64_t base_address, uint64_t pc,
                                  const char* msg) {
  // Wait until the exception is hit.
  FXL_DCHECK(exception.has_value());
  FXL_DCHECK(exception->pc > pc);
  FXL_DCHECK(DecodeHWException(thread, exception.value()) == HWExceptionType::kWatchpoint);
  pc = exception->pc;
  FXL_DCHECK(pc < base_address + kInstructionBufferSize * sizeof(uint64_t));
  uint32_t instruction = instructions[(pc - base_address) >> 2];
  FXL_DCHECK(instruction == expected_instruction);
  PRINT("SERVER: Exception on %p (0x%x): %s.", reinterpret_cast<void*>(pc), instruction, msg);

  return pc;
}

void WatchpointServer() {
  PRINT("Running Watchpoint Server");

  zx::job default_job(zx_job_default());
  zx::job child_job;
  CHECK_OK(zx::job::create(default_job, 0, &child_job));

  // Spawn a process the FDIO way.
  Process process;
  std::vector<std::string> args = {"/pkg/bin/hw_breakpointer", "watchpoint_client"};
  CHECK_OK(LaunchProcess(child_job, "test-process", args, &process));

  zx::eventpair event, theirs;
  CHECK_OK(zx::eventpair::create(0, &event, &theirs));

  // Send an event down the channel.
  zx_handle_t theirs_handle = theirs.release();
  CHECK_OK(process.comm_channel.write(0, &kIterations, sizeof(kIterations), &theirs_handle, 1));

  // Wait on the event.
  CHECK_OK(WaitForClient(event, DEADLINE(1)));
  PRINT("SERVER: Client got the event.");

  // We set up the exception channel.
  zx::port port;
  CHECK_OK(zx::port::create(0, &port));
  zx::channel exception_channel;
  CHECK_OK(process.handle.create_exception_channel(0, &exception_channel));
  WaitAsyncOnExceptionChannel(port, exception_channel);

  // Wait until the client sends us where the addresses are.
  CHECK_OK(WaitOnChannelReadable(process.comm_channel, DEADLINE(1)));
  uint64_t kAddresses[4];
  CHECK_OK(
      process.comm_channel.read(0, kAddresses, nullptr, sizeof(kAddresses), 0, nullptr, nullptr));

  PRINT("SERVER: SomeInt:  0x%p", reinterpret_cast<void*>(kAddresses[0]));
  PRINT("SERVER: SomeInt2: 0x%p", reinterpret_cast<void*>(kAddresses[1]));
  PRINT("SERVER: SomeInt3: 0x%p", reinterpret_cast<void*>(kAddresses[2]));
  PRINT("SERVER: SomeInt4: 0x%p", reinterpret_cast<void*>(kAddresses[3]));

  // Read the instructions.
  CHECK_OK(WaitOnChannelReadable(process.comm_channel, DEADLINE(1)));
  uint32_t kInstructions[kInstructionBufferSize] = {};
  CHECK_OK(process.comm_channel.read(0, kInstructions, nullptr, sizeof(kInstructions), 0, nullptr,
                                     nullptr));

  // Read the base address.
  CHECK_OK(WaitOnChannelReadable(process.comm_channel, DEADLINE(1)));
  uint64_t kBaseAddress = 0;
  CHECK_OK(process.comm_channel.read(0, &kBaseAddress, nullptr, sizeof(kBaseAddress), 0, nullptr,
                                     nullptr));
  FXL_DCHECK(kBaseAddress > 0);
  PRINT("SERVER: Got Base address %p.", reinterpret_cast<void*>(kBaseAddress));

  // Ping the client we got it and wait for it to spawn up a thread and send the handle over.
  CHECK_OK(SignalClient(event));
  CHECK_OK(WaitForClient(event, DEADLINE(1)));
  CHECK_OK(WaitOnChannelReadable(process.comm_channel, DEADLINE(1)));

  zx::thread thread;
  CHECK_OK(process.comm_channel.read(0, nullptr, thread.reset_and_get_address(), 0, 1, nullptr,
                                     nullptr));
  PRINT("SERVER: Received the thread handle.");

  // Setup a watchpoint.
  uint64_t wp_address = kAddresses[0];
  InstallWatchpoint(thread, wp_address, 4, WatchpointType::kReadWrite);

  CHECK_OK(SignalClient(event));
  for (uint32_t i = 0; i < kIterations; i++) {
    uint64_t pc = 0;
    PRINT("SERVER: ITERATION %d ---------------------------------------------------------", i);

    // printf 1.
    auto exception = WaitForException(port, exception_channel, DEADLINE(1));
    pc = CheckWatchpointException(exception, thread, port, exception_channel, kInstructions,
                                  kPrintLoad1, kBaseAddress, pc, "First printf read");
    WatchpointStepOver(wp_address, thread, port, exception_channel, std::move(exception));

    // ++ Read.
    exception = WaitForException(port, exception_channel, DEADLINE(1));
    pc = CheckWatchpointException(exception, thread, port, exception_channel, kInstructions,
                                  kPlusRead, kBaseAddress, pc, "++ read");
    WatchpointStepOver(wp_address, thread, port, exception_channel, std::move(exception));

    // ++ Write.
    exception = WaitForException(port, exception_channel, DEADLINE(1));
    pc = CheckWatchpointException(exception, thread, port, exception_channel, kInstructions,
                                  kPlusWrite, kBaseAddress, pc, "++ write");
    WatchpointStepOver(wp_address, thread, port, exception_channel, std::move(exception));

    // printf 2.
    exception = WaitForException(port, exception_channel, DEADLINE(1));
    pc = CheckWatchpointException(exception, thread, port, exception_channel, kInstructions,
                                  kPrintLoad2, kBaseAddress, pc, "Second printf read");
    WatchpointStepOver(wp_address, thread, port, exception_channel, std::move(exception));
  }

  // Wait for the client to be done.
  CHECK_OK(WaitForClient(event, DEADLINE(1)));
}

void WatchpointClient() {
  zx::channel channel;
  CHECK_OK(InitSubProcess(&channel));

  CHECK_OK(WaitOnChannelReadable(channel, DEADLINE(1)));

  uint32_t kTimes = 0;
  zx::eventpair event;
  CHECK_OK(
      channel.read(0, &kTimes, event.reset_and_get_address(), sizeof(kTimes), 1, nullptr, nullptr));
  FXL_DCHECK(kTimes > 0);

  PRINT("CLIENT: Read event. Times: %u.", kTimes);
  CHECK_OK(SignalServer(event));

  // Send over the addresses of the watchpoints.
  uint64_t kAddresses[4] = {(uint64_t)&SomeInt, (uint64_t)&SomeInt2, (uint64_t)&SomeInt3,
                            (uint64_t)&SomeInt3};
  CHECK_OK(channel.write(0, kAddresses, sizeof(kAddresses), nullptr, 0));

  PRINT("CLIENT: SomeInt:  0x%p", reinterpret_cast<void*>(kAddresses[0]));
  PRINT("CLIENT: SomeInt2: 0x%p", reinterpret_cast<void*>(kAddresses[1]));
  PRINT("CLIENT: SomeInt3: 0x%p", reinterpret_cast<void*>(kAddresses[2]));
  PRINT("CLIENT: SomeInt4: 0x%p", reinterpret_cast<void*>(kAddresses[3]));
  PRINT("CLIENT: Wrote addresses.");

  // Send over the instructions of the function.
  uint32_t kInstructions[kInstructionBufferSize] = {};
  memcpy(kInstructions, reinterpret_cast<void*>(AlignedWatchpointThreadFunction),
         sizeof(kInstructions));
  CHECK_OK(channel.write(0, kInstructions, sizeof(kInstructions), nullptr, 0));

  // Send over the base address.
  uint64_t kBaseAddress = (uint64_t)AlignedWatchpointThreadFunction;
  CHECK_OK(channel.write(0, &kBaseAddress, sizeof(kBaseAddress), nullptr, 0));
  PRINT("CLIENT: Sent base address %p.", reinterpret_cast<void*>(kBaseAddress));

  // Wait for ack from the server.
  CHECK_OK(WaitForServer(event, DEADLINE(1)));

  // Start the thread.
  AlignedWatchpointUserData user_data = {};
  user_data.times = kTimes;
  auto thread_setup = CreateTestSetup(AlignedWatchpointThreadFunction, &user_data);

  // Write the thread handle over.
  zx::thread thread_to_send;
  CHECK_OK(thread_setup->thread.duplicate(ZX_RIGHT_SAME_RIGHTS, &thread_to_send));

  CHECK_OK(SignalServer(event));

  zx_handle_t handle = thread_to_send.release();
  CHECK_OK(channel.write(0, nullptr, 0, &handle, 1));

  PRINT("CLIENT: Created and sent the thread handle over.");

  // Tell the client we wrote.
  CHECK_OK(WaitForServer(event, DEADLINE(1)));
  PRINT("CLIENT: Starting test thread.");

  // Tell the test to run.
  CHECK_OK(thread_setup->event.signal(kThreadToHarness, kHarnessToThread));

  // Wait until the thread is done.
  CHECK_OK(thread_setup->event.wait_one(kThreadToHarness, zx::time::infinite(), nullptr));

  // Signal the server we're done.
  CHECK_OK(SignalServer(event));
}

}  // namespace

// Main --------------------------------------------------------------------------------------------

namespace {

struct TestCase {
  using TestFunction = void (*)();

  std::string name = nullptr;
  std::string description = nullptr;
  TestFunction test_function = nullptr;
};

TestCase kTestCases[] = {
    {"hw_breakpoints", "Call multiple HW breakpoints on different functions.",
     BreakOnFunctionTestCase},

    {"watchpoints", "Call multiple watchpoints.", WatchpointTestCase},

    {"aligned_watchpoints", "Call aligned R/W watchpoint", AlignedWatchpointTestCase},

    {"channel_calls",
     "Send multiple messages over a channel call and read from it after it is closed.",
     ChannelMessagingTestCase},

    {"watchpoint_server", "Will start a client process and sets up a R/W watchpoint on it.",
     WatchpointServer},
    {"watchpoint_client", "Started by |watchpoint_server|. Not meant to be run manually.",
     WatchpointClient},
};

void PrintUsage() {
  printf("Usage: hw_breakpointer <TEST CASE>\n");
  printf("Test cases are:\n");
  for (auto& test_case : kTestCases) {
    printf("- %s: %s\n", test_case.name.c_str(), test_case.description.c_str());
  }
  fflush(stdout);
}

TestCase::TestFunction GetTestCase(std::string test_name) {
  for (auto& test_case : kTestCases) {
    if (test_name == test_case.name)
      return test_case.test_function;
  }

  return nullptr;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 2) {
    PrintUsage();
    return 1;
  }

  const char* test_name = argv[1];
  auto* test_function = GetTestCase(test_name);
  if (!test_function) {
    printf("Unknown test case %s\n", test_name);
    PrintUsage();
    return 1;
  }

  test_function();
  return 0;
}
