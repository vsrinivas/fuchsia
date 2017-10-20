// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>
#include <vector>

#include <benchmark/benchmark.h>
#include <launchpad/launchpad.h>
#include <lib/fxl/logging.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include "channels.h"
#include "round_trips.h"

// This tests the round-trip time of various Zircon kernel IPC primitives.
// It measures the latency of sending a request to another thread or
// process and receiving a reply back.

namespace {

// Read a small message from a channel, blocking.  Returns false if the
// channel's peer was closed.
bool ChannelRead(zx_handle_t channel, uint32_t* msg) {
  zx_signals_t observed;
  zx_status_t status =
      zx_object_wait_one(channel, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                         ZX_TIME_INFINITE, &observed);
  FXL_CHECK(status == ZX_OK);
  if (observed & ZX_CHANNEL_PEER_CLOSED)
    return false;

  uint32_t bytes_read;
  status = zx_channel_read(channel, 0, msg, nullptr, sizeof(*msg), 0,
                           &bytes_read, nullptr);
  FXL_CHECK(status == ZX_OK);
  FXL_CHECK(bytes_read == sizeof(*msg));
  return true;
}

// Serve requests on a channel: for each message received, send a reply.
void ChannelServe(zx_handle_t channel) {
  for (;;) {
    uint32_t msg;
    if (!ChannelRead(channel, &msg))
      break;
    zx_status_t status =
        zx_channel_write(channel, 0, &msg, sizeof(msg), nullptr, 0);
    FXL_CHECK(status == ZX_OK);
  }
}

typedef void (*ThreadFunc)(std::vector<zx_handle_t> handles);
ThreadFunc GetThreadFunc(const char* name);

enum MultiProc {
  SingleProcess = 1,
  MultiProcess = 2,
};

// Helper class for launching a thread or a subprocess.
class ThreadOrProcess {
 public:
  ~ThreadOrProcess() {
    if (thread_.joinable())
      thread_.join();
    if (subprocess_ != ZX_HANDLE_INVALID) {
      // Join the process.
      FXL_CHECK(zx_object_wait_one(subprocess_, ZX_PROCESS_TERMINATED,
                                   ZX_TIME_INFINITE, nullptr) == ZX_OK);
      zx_handle_close(subprocess_);
    }
  }

  void Launch(const char* func_name,
              zx_handle_t* handles,
              uint32_t handle_count,
              MultiProc multiproc) {
    if (multiproc == MultiProcess) {
      const char* args[] = {HELPER_PATH, "--subprocess", func_name};
      launchpad_t* lp;
      launchpad_create(0, "test-process", &lp);
      launchpad_load_from_file(lp, args[0]);
      launchpad_set_args(lp, countof(args), args);
      launchpad_clone(lp, LP_CLONE_ALL);
      uint32_t handle_types[handle_count];
      for (uint32_t i = 0; i < handle_count; ++i)
        handle_types[i] = PA_HND(PA_USER0, i);
      launchpad_add_handles(lp, handle_count, handles, handle_types);
      const char* errmsg;
      if (launchpad_go(lp, &subprocess_, &errmsg) != ZX_OK)
        FXL_LOG(FATAL) << "Subprocess launch failed: " << errmsg;
    } else {
      std::vector<zx_handle_t> handle_vector(handles, handles + handle_count);
      thread_ = std::thread(GetThreadFunc(func_name), handle_vector);
    }
  }

 private:
  std::thread thread_;
  zx_handle_t subprocess_ = ZX_HANDLE_INVALID;
};

// Test IPC round trips using Zircon channels where the client and server
// both use zx_object_wait_one() to wait.
class BasicChannelTest {
 public:
  BasicChannelTest(MultiProc multiproc) {
    zx_handle_t server;
    FXL_CHECK(zx_channel_create(0, &server, &client_) == ZX_OK);
    thread_or_process_.Launch("BasicChannelTest::ThreadFunc", &server, 1,
                              multiproc);
  }

  ~BasicChannelTest() { zx_handle_close(client_); }

  static void ThreadFunc(std::vector<zx_handle_t> handles) {
    FXL_CHECK(handles.size() == 1);
    zx_handle_t channel = handles[0];
    ChannelServe(channel);
    zx_handle_close(channel);
  }

  void Run() {
    uint32_t msg = 123;
    FXL_CHECK(zx_channel_write(client_, 0, &msg, sizeof(msg), nullptr, 0) ==
              ZX_OK);
    FXL_CHECK(ChannelRead(client_, &msg));
  }

 private:
  zx_handle_t client_;
  ThreadOrProcess thread_or_process_;
};

// Test IPC round trips using Zircon channels where the server uses
// zx_object_wait_one() to wait (as with BasicChannelTest) but the client
// uses zx_channel_call() for the send+wait+read.
class ChannelCallTest {
 public:
  ChannelCallTest(MultiProc multiproc) {
    zx_handle_t server;
    FXL_CHECK(zx_channel_create(0, &server, &client_) == ZX_OK);
    thread_or_process_.Launch("ChannelCallTest::ThreadFunc", &server, 1,
                              multiproc);

    msg_ = 0;
    args_.wr_bytes = reinterpret_cast<void*>(&msg_);
    args_.wr_handles = nullptr;
    args_.rd_bytes = reinterpret_cast<void*>(&reply_);
    args_.rd_handles = nullptr;
    args_.wr_num_bytes = sizeof(msg_);
    args_.wr_num_handles = 0;
    args_.rd_num_bytes = sizeof(reply_);
    args_.rd_num_handles = 0;
  }

  ~ChannelCallTest() { zx_handle_close(client_); }

  static void ThreadFunc(std::vector<zx_handle_t> handles) {
    FXL_CHECK(handles.size() == 1);
    zx_handle_t channel = handles[0];
    ChannelServe(channel);
    zx_handle_close(channel);
  }

  void Run() {
    uint32_t bytes_read;
    uint32_t handles_read;
    zx_status_t read_status;
    zx_status_t status =
        zx_channel_call(client_, 0, ZX_TIME_INFINITE, &args_, &bytes_read,
                        &handles_read, &read_status);
    FXL_CHECK(status == ZX_OK);
  }

 private:
  zx_handle_t client_;
  ThreadOrProcess thread_or_process_;
  uint32_t msg_;
  uint32_t reply_;
  zx_channel_call_args_t args_;
};

// Test IPC round trips using Zircon ports, where the client and server
// send each other user packets.  This is not a normal use case for ports,
// but it is useful for measuring the overhead of ports.
class PortTest {
 public:
  PortTest(MultiProc multiproc) {
    FXL_CHECK(zx_port_create(0, &ports_[0]) == ZX_OK);
    FXL_CHECK(zx_port_create(0, &ports_[1]) == ZX_OK);

    zx_handle_t ports_dup[2];
    for (int i = 0; i < 2; ++i) {
      FXL_CHECK(zx_handle_duplicate(ports_[i], ZX_RIGHT_SAME_RIGHTS,
                                    &ports_dup[i]) == ZX_OK);
    }
    thread_or_process_.Launch("PortTest::ThreadFunc", ports_dup,
                              countof(ports_dup), multiproc);
  }

  ~PortTest() {
    // Tell the server to shut down.
    zx_port_packet_t packet = {};
    packet.type = ZX_PKT_TYPE_USER;
    packet.user.u32[0] = 1;
    FXL_CHECK(zx_port_queue(ports_[0], &packet, 0) == ZX_OK);

    zx_handle_close(ports_[0]);
    zx_handle_close(ports_[1]);
  }

  static void ThreadFunc(std::vector<zx_handle_t> ports) {
    FXL_CHECK(ports.size() == 2);
    for (;;) {
      zx_port_packet_t packet;
      FXL_CHECK(zx_port_wait(ports[0], ZX_TIME_INFINITE, &packet, 0) == ZX_OK);
      // Check for a request to shut down.
      if (packet.user.u32[0])
        break;
      FXL_CHECK(zx_port_queue(ports[1], &packet, 0) == ZX_OK);
    }
    zx_handle_close(ports[0]);
    zx_handle_close(ports[1]);
  }

  void Run() {
    zx_port_packet_t packet = {};
    packet.type = ZX_PKT_TYPE_USER;
    FXL_CHECK(zx_port_queue(ports_[0], &packet, 0) == ZX_OK);
    FXL_CHECK(zx_port_wait(ports_[1], ZX_TIME_INFINITE, &packet, 0) == ZX_OK);
  }

 private:
  zx_handle_t ports_[2];
  ThreadOrProcess thread_or_process_;
};

// Test the round trip time for waking up threads using Zircon futexes.
// Note that Zircon does not support cross-process futexes, only
// within-process futexes, so there is no multi-process version of this
// test case.
class FutexTest {
 public:
  FutexTest() {
    thread_ = std::thread([this]() { ThreadFunc(); });
  }

  ~FutexTest() {
    Wake(&futex1_, 2);  // Tell the thread to shut down.
    thread_.join();
  }

  void Run() {
    Wake(&futex1_, 1);
    FXL_CHECK(!Wait(&futex2_));
  }

 private:
  void ThreadFunc() {
    for (;;) {
      if (Wait(&futex1_))
        break;
      Wake(&futex2_, 1);
    }
  }

  void Wake(volatile int* ptr, int wake_value) {
    *ptr = wake_value;
    FXL_CHECK(zx_futex_wake(const_cast<int*>(ptr), 1) == ZX_OK);
  }

  bool Wait(volatile int* ptr) {
    for (;;) {
      int val = *ptr;
      if (val != 0) {
        // We were signaled.  Reset the state to unsignaled.
        *ptr = 0;
        // Return whether we got a request to shut down.
        return val == 2;
      }
      zx_status_t status =
          zx_futex_wait(const_cast<int*>(ptr), val, ZX_TIME_INFINITE);
      FXL_CHECK(status == ZX_OK || status == ZX_ERR_BAD_STATE);
    }
  }

  std::thread thread_;
  volatile int futex1_ = 0;  // Signals from client to server.
  volatile int futex2_ = 0;  // Signals from server to client.
};

struct ThreadFuncEntry {
  const char* name;
  ThreadFunc func;
};

const ThreadFuncEntry thread_funcs[] = {
#define DEF_FUNC(FUNC) { #FUNC, FUNC },
  DEF_FUNC(BasicChannelTest::ThreadFunc)
  DEF_FUNC(ChannelCallTest::ThreadFunc)
  DEF_FUNC(PortTest::ThreadFunc)
#undef DEF_FUNC
};

ThreadFunc GetThreadFunc(const char* name) {
  for (size_t i = 0; i < countof(thread_funcs); ++i) {
    if (!strcmp(name, thread_funcs[i].name))
      return thread_funcs[i].func;
  }
  FXL_LOG(FATAL) << "Thread function not found: " << name;
  return nullptr;
}

// Define benchmark entry points.  This involves a bit of code duplication.

void RoundTrip_BasicChannel_SingleProcess(benchmark::State& state) {
  BasicChannelTest test(SingleProcess);
  while (state.KeepRunning())
    test.Run();
}
BENCHMARK(RoundTrip_BasicChannel_SingleProcess);

void RoundTrip_BasicChannel_MultiProcess(benchmark::State& state) {
  BasicChannelTest test(MultiProcess);
  while (state.KeepRunning())
    test.Run();
}
BENCHMARK(RoundTrip_BasicChannel_MultiProcess);

void RoundTrip_ChannelCall_SingleProcess(benchmark::State& state) {
  ChannelCallTest test(SingleProcess);
  while (state.KeepRunning())
    test.Run();
}
BENCHMARK(RoundTrip_ChannelCall_SingleProcess);

void RoundTrip_ChannelCall_MultiProcess(benchmark::State& state) {
  ChannelCallTest test(MultiProcess);
  while (state.KeepRunning())
    test.Run();
}
BENCHMARK(RoundTrip_ChannelCall_MultiProcess);

void RoundTrip_Port_SingleProcess(benchmark::State& state) {
  PortTest test(SingleProcess);
  while (state.KeepRunning())
    test.Run();
}
BENCHMARK(RoundTrip_Port_SingleProcess);

void RoundTrip_Port_MultiProcess(benchmark::State& state) {
  PortTest test(MultiProcess);
  while (state.KeepRunning())
    test.Run();
}
BENCHMARK(RoundTrip_Port_MultiProcess);

void RoundTrip_Futex_SingleProcess(benchmark::State& state) {
  FutexTest test;
  while (state.KeepRunning())
    test.Run();
}
BENCHMARK(RoundTrip_Futex_SingleProcess);

}  // namespace

void RunSubprocess(const char* func_name) {
  auto func = GetThreadFunc(func_name);
  // Retrieve the handles.
  std::vector<zx_handle_t> handles;
  for (;;) {
    zx_handle_t handle =
        zx_get_startup_handle(PA_HND(PA_USER0, handles.size()));
    if (handle == ZX_HANDLE_INVALID)
      break;
    handles.push_back(handle);
  }
  func(handles);
}
