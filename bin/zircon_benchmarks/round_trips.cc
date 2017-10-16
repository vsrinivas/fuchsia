// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include <benchmark/benchmark.h>
#include <launchpad/launchpad.h>
#include <lib/fxl/logging.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

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

typedef void (*ThreadFunc)(zx_handle_t handle);
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
              zx_handle_t handle_arg,
              MultiProc multiproc) {
    if (multiproc == MultiProcess) {
      const char* args[] = {HELPER_PATH, "--subprocess", func_name};
      launchpad_t* lp;
      launchpad_create(0, "test-process", &lp);
      launchpad_load_from_file(lp, args[0]);
      launchpad_set_args(lp, countof(args), args);
      launchpad_clone(lp, LP_CLONE_ALL);
      zx_handle_t handles[] = {handle_arg};
      uint32_t handle_types[] = {PA_HND(PA_USER0, 0)};
      launchpad_add_handles(lp, countof(handles), handles, handle_types);
      const char* errmsg;
      if (launchpad_go(lp, &subprocess_, &errmsg) != ZX_OK)
        FXL_LOG(FATAL) << "Subprocess launch failed: " << errmsg;
    } else {
      thread_ = std::thread(GetThreadFunc(func_name), handle_arg);
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
    thread_or_process_.Launch("BasicChannelTest::ThreadFunc", server,
                              multiproc);
  }

  ~BasicChannelTest() { zx_handle_close(client_); }

  static void ThreadFunc(zx_handle_t channel) {
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
    thread_or_process_.Launch("ChannelCallTest::ThreadFunc", server, multiproc);

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

  static void ThreadFunc(zx_handle_t channel) {
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

struct ThreadFuncEntry {
  const char* name;
  ThreadFunc func;
};

const ThreadFuncEntry thread_funcs[] = {
#define DEF_FUNC(FUNC) { #FUNC, FUNC },
  DEF_FUNC(BasicChannelTest::ThreadFunc)
  DEF_FUNC(ChannelCallTest::ThreadFunc)
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

}  // namespace

void RunSubprocess(const char* func_name) {
  auto func = GetThreadFunc(func_name);
  func(zx_get_startup_handle(PA_HND(PA_USER0, 0)));
}
