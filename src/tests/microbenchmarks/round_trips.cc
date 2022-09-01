// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "round_trips.h"

#include <fidl/fuchsia.scheduler/cpp/wire.h>
#include <fuchsia/zircon/benchmarks/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <pthread.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <functional>
#include <iterator>
#include <thread>
#include <vector>

#include "assert.h"
#include "lib/fidl/cpp/binding.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "test_runner.h"

// This file measures two things:
//
// 1) The round-trip time of various operations, including Zircon kernel IPC
// primitives.  This measures the latency of sending a request to another thread
// or process and receiving a reply back.  In this case, there's little
// opportunity for concurrency between the two threads.
//
// 2) The throughput of IPC operations.  This is similar to measuring the
// round-trip time, except that instead of sending and receiving one message,
// the main thread sends N messages and then waits for N messages in reply.
// This allows for more concurrency between the two threads.  Currently we only
// test this for Zircon channels.
//
// Note that the first case is a special case of the second case, with N=1.
//
// These tests generally use the same IPC primitive in both directions
// (i.e. from client to server and from server to client) for sending and
// receiving wakeups.  There are a couple of reasons for that:
//
//  * This allows us to estimate the one-way latency of the IPC primitive
//    by dividing the round-trip latency by 2.
//  * This keeps the number of tests manageable.  If we mixed the
//    primitives, the number of possible combinations would be O(n^2) in
//    the number of primitives.  (For example, we could signal using a
//    channel in one direction and a futex in the other direction.)
//
// An exception is zx_channel_call(), which generally can't be used by a
// server process for receiving requests.
//
// There are two further dimensions of test variants:
//
//  * "SingleProcess" versus "MultiProcess".  The single-process case
//    involves round trips between two threads in the same process,
//    whereas the multi-process case involves round trips between two
//    threads in different processes.
//
//    The multi-process case tends to be slower as a result of
//    requiring TLB flushes (or similar operations) when switching
//    between processes (if the processes are scheduled on the same
//    CPU).
//
//  * "SameCpu" versus "DiffCpu".  These variants set the CPU
//    affinities of the two threads so that the threads are pinned to
//    the same CPU or different CPUs.
//
//    The different-CPU case might be faster as a result of the
//    increased parallelism, or it might be slower as a result of IPI
//    latency and lock contention between the CPUs.

namespace {

// Block and read a message of size |msg->size()| into |msg| from a channel.
// Returns false if the channel's peer was closed.
bool ChannelRead(const zx::channel& channel, std::vector<uint8_t>* msg) {
  zx_signals_t observed;
  ASSERT_OK(channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                             &observed));
  if (observed & ZX_CHANNEL_PEER_CLOSED)
    return false;

  uint32_t bytes_read;
  ASSERT_OK(channel.read(0, msg->data(), nullptr, static_cast<uint32_t>(msg->size()), 0,
                         &bytes_read, nullptr));
  FX_CHECK(bytes_read == msg->size());
  return true;
}

// Block and read |count| messages of size |msg->size()| into |msg| from a
// channel.  Returns false if the channel's peer was closed.
bool ChannelReadMultiple(const zx::channel& channel, uint32_t count, std::vector<uint8_t>* msg) {
  for (uint32_t i = 0; i < count; ++i) {
    if (!ChannelRead(channel, msg))
      return false;
  }
  return true;
}

// Serve requests on a channel: read |count| messages of size |size| and write
// |count| replies.
void ChannelServe(const zx::channel& channel, uint32_t count, uint32_t size) {
  std::vector<uint8_t> msg(size);
  for (;;) {
    if (!ChannelReadMultiple(channel, count, &msg))
      break;
    for (uint32_t i = 0; i < count; ++i) {
      ASSERT_OK(channel.write(0, msg.data(), static_cast<uint32_t>(msg.size()), nullptr, 0));
    }
  }
}

// Set the CPU affinity for the current thread.  This allows setting
// only the bottom 32 bits of the CPU affinity mask, but that is
// enough for pinning threads to the same or different CPUs.
void SetCpuAffinity(uint32_t cpu_mask) {
  if (cpu_mask == 0)
    return;

  auto endpoints = fidl::CreateEndpoints<fuchsia_scheduler::ProfileProvider>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(fdio_service_connect_by_name(
      fidl::DiscoverableProtocolName<fuchsia_scheduler::ProfileProvider>,
      endpoints->server.channel().release()));
  auto provider = std::move(endpoints->client);

  fuchsia_scheduler::wire::CpuSet cpu_set = {};
  cpu_set.mask[0] = cpu_mask;
  auto result = fidl::WireCall(provider)->GetCpuAffinityProfile(cpu_set);
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ASSERT_OK(zx::thread::self()->set_profile(result.value().profile, 0));
}

typedef void (*ThreadFunc)(std::vector<zx::handle>&& handles);
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
    if (subprocess_) {
      // Join the process.
      ASSERT_OK(subprocess_.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr));
    }
  }

  void LaunchWithCpuAffinity(const char* func_name, std::vector<zx::handle>&& handles,
                             MultiProc multiproc, uint32_t cpu_mask) {
    if (multiproc == MultiProcess) {
      const char* executable_path = "/pkg/bin/fuchsia_microbenchmarks";
      std::string cpu_mask_arg = fxl::NumberToString(cpu_mask);
      const char* args[] = {executable_path, "--subprocess", func_name, cpu_mask_arg.c_str(),
                            nullptr};
      size_t action_count = handles.size() + 1;
      fdio_spawn_action_t actions[action_count];
      for (uint32_t i = 0; i < handles.size(); ++i) {
        actions[i].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
        actions[i].h.id = PA_HND(PA_USER0, i);
        actions[i].h.handle = handles[i].release();
      }
      actions[handles.size()].action = FDIO_SPAWN_ACTION_SET_NAME;
      actions[handles.size()].name.data = "test-process";

      char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
      if (fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, executable_path, args, nullptr,
                         action_count, actions, subprocess_.reset_and_get_address(),
                         err_msg) != ZX_OK) {
        FX_LOGS(FATAL) << "Subprocess launch failed: " << err_msg;
      }
    } else {
      auto thread_func = [=](std::vector<zx::handle>&& handles) {
        SetCpuAffinity(cpu_mask);
        GetThreadFunc(func_name)(std::move(handles));
      };
      thread_ = std::thread(thread_func, std::move(handles));
    }
  }

  void Launch(const char* func_name, std::vector<zx::handle>&& handles, MultiProc multiproc) {
    LaunchWithCpuAffinity(func_name, std::move(handles), multiproc, 0);
  }

 private:
  std::thread thread_;
  zx::process subprocess_;
};

// Convenience function for creating a vector of zx::handles.
std::vector<zx::handle> MakeHandleVector(zx_handle_t handle) {
  // Note that "std::vector<zx::handle> v{h}" creates a vector of size h,
  // which is not what we want.
  std::vector<zx::handle> vec(1);
  vec[0] = zx::handle(handle);
  return vec;
}

// Test IPC round trips and/or throughput using Zircon channels where the client
// and server both use zx_object_wait_one() to wait.
class BasicChannelTest {
 public:
  explicit BasicChannelTest(MultiProc multiproc, uint32_t msg_count, uint32_t msg_size)
      : args_({msg_count, msg_size}), msg_(args_.msg_size) {
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &server, &client_));
    thread_or_process_.Launch("BasicChannelTest::ThreadFunc", MakeHandleVector(server.release()),
                              multiproc);

    // Pass the test arguments to the other thread.
    ASSERT_OK(client_.write(0, &args_, sizeof(args_), nullptr, 0));
  }

  static void ThreadFunc(std::vector<zx::handle>&& handles) {
    FX_CHECK(handles.size() == 1);
    zx::channel channel(std::move(handles[0]));
    Args args;
    GetArgs(channel, &args);
    ChannelServe(channel, args.msg_count, args.msg_size);
  }

  void Run() {
    for (unsigned i = 0; i < args_.msg_count; ++i) {
      ASSERT_OK(client_.write(0, msg_.data(), static_cast<uint32_t>(msg_.size()), nullptr, 0));
    }
    FX_CHECK(ChannelReadMultiple(client_, args_.msg_count, &msg_));
  }

 private:
  // Holds the test arguments sent over a channel.
  struct Args {
    uint32_t msg_count;
    uint32_t msg_size;
  };

  // Reads test arguments from |channel| and stores them in |args|.
  static void GetArgs(const zx::channel& channel, Args* args) {
    std::vector<uint8_t> msg(sizeof(*args));
    FX_CHECK(ChannelRead(channel, &msg));
    *args = *reinterpret_cast<Args*>(msg.data());
  }

  const Args args_;
  std::vector<uint8_t> msg_;
  ThreadOrProcess thread_or_process_;
  zx::channel client_;
};

// Test IPC round trips using Zircon channels where the client and server
// both use Zircon ports to wait.
class ChannelPortTest {
 public:
  explicit ChannelPortTest(MultiProc multiproc, uint32_t child_thread_cpu_mask = 0) {
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &server, &client_));
    thread_or_process_.LaunchWithCpuAffinity("ChannelPortTest::ThreadFunc",
                                             MakeHandleVector(server.release()), multiproc,
                                             child_thread_cpu_mask);
    ASSERT_OK(zx::port::create(0, &client_port_));
  }

  static bool ChannelPortRead(const zx::channel& channel, const zx::port& port, uint32_t* msg) {
    ASSERT_OK(channel.wait_async(port, 0, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0));

    zx_port_packet_t packet;
    ASSERT_OK(port.wait(zx::time::infinite(), &packet));
    if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED)
      return false;

    uint32_t bytes_read;
    ASSERT_OK(channel.read(0, msg, nullptr, sizeof(*msg), 0, &bytes_read, nullptr));
    FX_CHECK(bytes_read == sizeof(*msg));
    return true;
  }

  static void ThreadFunc(std::vector<zx::handle>&& handles) {
    FX_CHECK(handles.size() == 1);
    zx::channel channel(std::move(handles[0]));

    zx::port port;
    ASSERT_OK(zx::port::create(0, &port));

    for (;;) {
      uint32_t msg;
      if (!ChannelPortRead(channel, port, &msg))
        break;
      ASSERT_OK(channel.write(0, &msg, sizeof(msg), nullptr, 0));
    }
  }

  void Run() {
    uint32_t msg = 123;
    ASSERT_OK(client_.write(0, &msg, sizeof(msg), nullptr, 0));
    FX_CHECK(ChannelPortRead(client_, client_port_, &msg));
  }

 private:
  ThreadOrProcess thread_or_process_;
  zx::channel client_;
  zx::port client_port_;
};

// Test IPC round trips using Zircon channels where the server uses
// zx_object_wait_one() to wait (as with BasicChannelTest) but the client
// uses zx_channel_call() for the send+wait+read.
class ChannelCallTest {
 public:
  explicit ChannelCallTest(MultiProc multiproc) {
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &server, &client_));
    thread_or_process_.Launch("ChannelCallTest::ThreadFunc", MakeHandleVector(server.release()),
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

  static void ThreadFunc(std::vector<zx::handle>&& handles) {
    FX_CHECK(handles.size() == 1);
    zx::channel channel(std::move(handles[0]));
    ChannelServe(channel, /* count= */ 1, /* size= */ 4);
  }

  void Run() {
    uint32_t bytes_read;
    uint32_t handles_read;
    ASSERT_OK(client_.call(0, zx::time::infinite(), &args_, &bytes_read, &handles_read));
  }

 private:
  ThreadOrProcess thread_or_process_;
  zx::channel client_;
  uint32_t msg_;
  uint32_t reply_;
  zx_channel_call_args_t args_;
};

// Test IPC round trips using Zircon ports, where the client and server
// send each other user packets.  This is not a normal use case for ports,
// but it is useful for measuring the overhead of ports.
class PortTest {
 public:
  explicit PortTest(MultiProc multiproc) {
    ASSERT_OK(zx::port::create(0, &ports_[0]));
    ASSERT_OK(zx::port::create(0, &ports_[1]));

    std::vector<zx::handle> ports_dup(2);
    for (int i = 0; i < 2; ++i) {
      zx::port dup;
      ASSERT_OK(ports_[i].duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
      ports_dup[i] = std::move(dup);
    }
    thread_or_process_.Launch("PortTest::ThreadFunc", std::move(ports_dup), multiproc);
  }

  ~PortTest() {
    // Tell the server to shut down.
    zx_port_packet_t packet = {};
    packet.type = ZX_PKT_TYPE_USER;
    packet.user.u32[0] = 1;
    ASSERT_OK(ports_[0].queue(&packet));
  }

  static void ThreadFunc(std::vector<zx::handle>&& ports) {
    FX_CHECK(ports.size() == 2);
    for (;;) {
      zx_port_packet_t packet;
      ASSERT_OK(zx_port_wait(ports[0].get(), ZX_TIME_INFINITE, &packet));
      // Check for a request to shut down.
      if (packet.user.u32[0])
        break;
      ASSERT_OK(zx_port_queue(ports[1].get(), &packet));
    }
  }

  void Run() {
    zx_port_packet_t packet = {};
    packet.type = ZX_PKT_TYPE_USER;
    ASSERT_OK(ports_[0].queue(&packet));
    ASSERT_OK(ports_[1].wait(zx::time::infinite(), &packet));
  }

 private:
  zx::port ports_[2];
  ThreadOrProcess thread_or_process_;
};

// Helper object for signaling and waiting on a Zircon event object.  This
// uses a port for waiting on the event object.
class EventPortSignaler {
 public:
  EventPortSignaler() { ASSERT_OK(zx::port::create(0, &port_)); }

  void set_event(zx::eventpair&& event) { event_ = std::move(event); }

  // Waits for the event to be signaled.  Returns true if it was signaled
  // by Signal() and false if the peer event object was closed.
  bool Wait() {
    ASSERT_OK(event_.wait_async(port_, 0, ZX_USER_SIGNAL_0 | ZX_EVENTPAIR_PEER_CLOSED, 0));
    zx_port_packet_t packet;
    ASSERT_OK(port_.wait(zx::time::infinite(), &packet));
    if (packet.signal.observed & ZX_EVENTPAIR_PEER_CLOSED)
      return false;
    // Clear the signal bit.
    ASSERT_OK(event_.signal(ZX_USER_SIGNAL_0, 0));
    return true;
  }

  void Signal() {
    // Set a signal bit.
    ASSERT_OK(event_.signal_peer(0, ZX_USER_SIGNAL_0));
  }

 private:
  zx::eventpair event_;
  zx::port port_;
};

// Test the round trip time for waking up threads by signaling using Zircon
// event objects.  This uses ports for waiting on the events (rather than
// zx_object_wait_one()), because ports are the most general way to wait.
class EventPortTest {
 public:
  explicit EventPortTest(MultiProc multiproc) {
    zx::eventpair event1;
    zx::eventpair event2;
    ASSERT_OK(zx::eventpair::create(0, &event1, &event2));
    signaler_.set_event(std::move(event1));

    thread_or_process_.Launch("EventPortTest::ThreadFunc", MakeHandleVector(event2.release()),
                              multiproc);
  }

  static void ThreadFunc(std::vector<zx::handle>&& handles) {
    FX_CHECK(handles.size() == 1);

    EventPortSignaler signaler;
    signaler.set_event(zx::eventpair(handles[0].get()));
    while (signaler.Wait()) {
      signaler.Signal();
    }
  }

  void Run() {
    signaler_.Signal();
    FX_CHECK(signaler_.Wait());
  }

 private:
  ThreadOrProcess thread_or_process_;
  EventPortSignaler signaler_;
};

// Helper object for signaling and waiting on a Zircon socket object.  This
// uses a port for waiting on the socket object.
class SocketPortSignaler {
 public:
  SocketPortSignaler() { ASSERT_OK(zx::port::create(0, &port_)); }

  void set_socket(zx::socket&& socket) { socket_ = std::move(socket); }

  // Waits for the socket to be signaled: reads a byte from the socket.
  // Returns true if it was signaled by Signal() and false if it was
  // signaled by SignalExit().
  bool Wait() {
    ASSERT_OK(socket_.wait_async(port_, 0, ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, 0));
    zx_port_packet_t packet;
    ASSERT_OK(port_.wait(zx::time::infinite(), &packet));
    if (packet.signal.observed & ZX_SOCKET_PEER_CLOSED)
      return false;
    uint8_t message;
    size_t bytes_read = 0;
    ASSERT_OK(socket_.read(0, &message, 1, &bytes_read));
    FX_CHECK(bytes_read == 1);
    return true;
  }

  // Signal the socket by writing a byte to it.
  void Signal() {
    uint8_t message = 0;
    size_t bytes_written = 0;
    ASSERT_OK(socket_.write(0, &message, 1, &bytes_written));
    FX_CHECK(bytes_written == 1);
  }

 private:
  zx::socket socket_;
  zx::port port_;
};

// Test the round trip time for waking up threads by reading and writing
// bytes on Zircon socket objects.  This uses ports for waiting on the
// sockets (rather than zx_object_wait_one()), because ports are the most
// general way to wait.
class SocketPortTest {
 public:
  explicit SocketPortTest(MultiProc multiproc) {
    zx::socket socket1;
    zx::socket socket2;
    ASSERT_OK(zx::socket::create(0, &socket1, &socket2));
    signaler_.set_socket(std::move(socket1));

    thread_or_process_.Launch("SocketPortTest::ThreadFunc", MakeHandleVector(socket2.release()),
                              multiproc);
  }

  static void ThreadFunc(std::vector<zx::handle>&& handles) {
    FX_CHECK(handles.size() == 1);

    SocketPortSignaler signaler;
    signaler.set_socket(zx::socket(handles[0].get()));
    while (signaler.Wait()) {
      signaler.Signal();
    }
  }

  void Run() {
    signaler_.Signal();
    FX_CHECK(signaler_.Wait());
  }

 private:
  ThreadOrProcess thread_or_process_;
  SocketPortSignaler signaler_;
};

// Implementation of FIDL interface for testing round trip IPCs.
class RoundTripperImpl : public fuchsia::zircon::benchmarks::RoundTripper {
 public:
  void RoundTripTest(uint32_t arg, RoundTripTestCallback callback) override {
    FX_CHECK(arg == 123);
    callback(456);
  }
};

// Test IPC round trips using FIDL IPC.  This uses a synchronous IPC on the
// client side.
class FidlTest {
 public:
  explicit FidlTest(MultiProc multiproc) {
    zx_handle_t server = service_ptr_.NewRequest().TakeChannel().release();
    thread_or_process_.Launch("FidlTest::ThreadFunc", MakeHandleVector(server), multiproc);
  }

  static void ThreadFunc(std::vector<zx::handle>&& handles) {
    FX_CHECK(handles.size() == 1);
    zx::channel channel(std::move(handles[0]));

    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    RoundTripperImpl service_impl;
    fidl::Binding<fuchsia::zircon::benchmarks::RoundTripper> binding(&service_impl,
                                                                     std::move(channel));
    binding.set_error_handler([&loop](zx_status_t status) { loop.Quit(); });
    loop.Run();
  }

  void Run() {
    uint32_t result;
    ASSERT_OK(service_ptr_->RoundTripTest(123, &result));
    FX_CHECK(result == 456);
  }

 private:
  ThreadOrProcess thread_or_process_;
  fuchsia::zircon::benchmarks::RoundTripperSyncPtr service_ptr_;
};

struct ThreadFuncEntry {
  const char* name;
  ThreadFunc func;
};

// clang-format off
const ThreadFuncEntry thread_funcs[] = {
#define DEF_FUNC(FUNC) { #FUNC, FUNC },
  DEF_FUNC(BasicChannelTest::ThreadFunc)
  DEF_FUNC(ChannelPortTest::ThreadFunc)
  DEF_FUNC(ChannelCallTest::ThreadFunc)
  DEF_FUNC(PortTest::ThreadFunc)
  DEF_FUNC(EventPortTest::ThreadFunc)
  DEF_FUNC(SocketPortTest::ThreadFunc)
  DEF_FUNC(FidlTest::ThreadFunc)
#undef DEF_FUNC
};
// clang-format on

ThreadFunc GetThreadFunc(const char* name) {
  for (size_t i = 0; i < std::size(thread_funcs); ++i) {
    if (!strcmp(name, thread_funcs[i].name))
      return thread_funcs[i].func;
  }
  FX_LOGS(FATAL) << "Thread function not found: " << name;
  return nullptr;
}

// Register a test that has two variants, single-process and multi-process.
template <class TestClass, typename... Args>
void RegisterTestMultiProc(const char* base_name, Args... args) {
  fbenchmark::RegisterTest<TestClass>((std::string(base_name) + "_SingleProcess").c_str(),
                                      SingleProcess, std::forward<Args>(args)...);
  fbenchmark::RegisterTest<TestClass>((std::string(base_name) + "_MultiProcess").c_str(),
                                      MultiProcess, std::forward<Args>(args)...);
}

// Call the given function with CPU affinity set to the given mask.
//
// Fuchsia does not currently provide a way to restore the zx::profile
// for a thread after setting it, so in order to leave the zx::profile
// of the calling thread unmodified, this creates a new thread for
// running the function.
void CallWithCpuAffinity(uint32_t cpu_mask, std::function<void()> func) {
  if (cpu_mask == 0) {
    // Simple case: Avoid the overhead of creating another thread, and
    // use the current thread.
    func();
  } else {
    std::thread thread([=] {
      SetCpuAffinity(cpu_mask);
      func();
    });
    thread.join();
  }
}

// Register a test where the Run() method is run on a thread with the
// given CPU affinity.
template <class TestClass, typename... Args>
void RegisterTestWithCpuAffinity(const char* test_name, uint32_t cpu_mask, Args... args) {
  perftest::RegisterTest(test_name, [=](perftest::RepeatState* state) {
    CallWithCpuAffinity(cpu_mask, [=] {
      TestClass test(args...);
      while (state->KeepRunning()) {
        test.Run();
      }
    });
    return true;
  });
}

// Register a test with instantiations covering the same-CPU and
// different-CPU cases as well as the single-process and multi-process
// cases.
template <class TestClass>
void RegisterTestMultiProcSameDiffCpu(const char* base_name) {
  struct MultiProcParam {
    const char* suffix;
    MultiProc value;
  };
  const static MultiProcParam multi_proc_params[] = {
      {"_SingleProcess", SingleProcess},
      {"_MultiProcess", MultiProcess},
  };

  struct CpuParam {
    const char* suffix;
    uint32_t parent_thread_cpu_mask;
    uint32_t child_thread_cpu_mask;
  };
  // These parameters pin the threads to CPUs 0 and 1.  This is
  // reasonable on systems with uniform CPUs, such as NUCs.  This
  // would need to be revisited for systems with non-uniform CPUs,
  // e.g. big.LITTLE systems such as VIM3s.  On a single-CPU system,
  // the pinning should have no effect.
  const static CpuParam cpu_params[] = {
      {"_SameCpu", 1, 1},
      {"_DiffCpu", 1, 2},
  };

  for (auto multi_proc_param : multi_proc_params) {
    for (auto cpu_param : cpu_params) {
      RegisterTestWithCpuAffinity<TestClass>(
          (std::string(base_name) + multi_proc_param.suffix + cpu_param.suffix).c_str(),
          cpu_param.parent_thread_cpu_mask, multi_proc_param.value,
          cpu_param.child_thread_cpu_mask);
    }
  };
}

void RegisterTests() {
  RegisterTestMultiProc<BasicChannelTest>("RoundTrip_BasicChannel",
                                          /* count= */ 1, /* size= */ 4);
  RegisterTestMultiProc<BasicChannelTest>("IpcThroughput_BasicChannel_1_64kbytes",
                                          /* msg_count= */ 1, /* msg_size= */ 64 * 1024);

  // These next two benchmarks allocate and free a significant amount of
  // memory so their performance can be heavily dependent on kernel allocator
  // performance.
  RegisterTestMultiProc<BasicChannelTest>("IpcThroughput_BasicChannel_1024_4bytes",
                                          /* msg_count= */ 1024, /* msg_size= */ 4);
  RegisterTestMultiProc<BasicChannelTest>("IpcThroughput_BasicChannel_1024_64kbytes",
                                          /* msg_count= */ 1024, /* msg_size= */ 64 * 1024);

  RegisterTestMultiProc<ChannelPortTest>("RoundTrip_ChannelPort");
  RegisterTestMultiProc<ChannelCallTest>("RoundTrip_ChannelCall");
  RegisterTestMultiProc<PortTest>("RoundTrip_Port");
  RegisterTestMultiProc<EventPortTest>("RoundTrip_EventPort");
  RegisterTestMultiProc<SocketPortTest>("RoundTrip_SocketPort");
  RegisterTestMultiProc<FidlTest>("RoundTrip_Fidl");

  // To avoid creating too many test instantiations and metrics, we
  // only instantiate one of these tests for the same-CPU and
  // different-CPU cases.
  RegisterTestMultiProcSameDiffCpu<ChannelPortTest>("RoundTrip_ChannelPort");
}
PERFTEST_CTOR(RegisterTests)

}  // namespace

void RunSubprocess(const char* func_name, const char* cpu_mask_arg) {
  auto func = GetThreadFunc(func_name);
  // Retrieve the handles.
  std::vector<zx::handle> handles;
  for (;;) {
    uint32_t index = static_cast<uint32_t>(handles.size());
    zx::handle handle(zx_take_startup_handle(PA_HND(PA_USER0, index)));
    if (!handle)
      break;
    handles.push_back(std::move(handle));
  }

  uint32_t cpu_mask;
  FX_CHECK(fxl::StringToNumberWithError(cpu_mask_arg, &cpu_mask));
  SetCpuAffinity(cpu_mask);

  func(std::move(handles));
}
