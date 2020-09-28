// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_TESTS_FUTEX_OWNERSHIP_UTILS_H_
#define SRC_ZIRCON_TESTS_FUTEX_OWNERSHIP_UTILS_H_

#include <lib/zx/channel.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <atomic>

#include <fbl/function.h>
#include <fbl/futex.h>
#include <fbl/macros.h>

// Allow up to 4 pointers worth of storage for any lambdas we need to capture
// into a fbl::InlineFunction object (instead of falling back on the implicit
// heap allocation behavior of fbl::Function or std::function)
static constexpr size_t kMaxLambdaStorage = sizeof(void*) * 4;

// TODO(fxbug.dev/55744): An extremely long timeout we use as a proxy for "forever".
// Someday, if the test framework ever gives us an environment specific timeout
// to use as a soft watchdog threshold, we should switch to using that instead.
static constexpr zx::duration kLongTimeout = zx::sec(25);

// A small helper which allows us to poll with a timeout for a condition to
// become true.  Sadly, some of the futex-ownership tests require this as there
// is no opportunity for a thread which has become blocked on a futex to signal
// another thread without unblocking.  When testing the state of the system
// while we have a thread blocked via zx_futex_wait, the best we can do is have
// the thread give us a signal, and then wait a "reasonable" amount of time for
// the system to achieve the desired state (or not).
using WaitFn = fbl::InlineFunction<bool(void), kMaxLambdaStorage>;
bool WaitFor(zx::duration timeout, WaitFn wait_fn);

// A small helper which fetches the Koid for the current thread.
zx_koid_t CurrentThreadKoid();

// A lightweight signal based on an unowned futex which can be used to
// block/unblock threads.  Used extensively in the futex ownership tests for
// sequencing thread behavior which would typically just be a bunch of timing
// races in real code.
class Event {
 public:
  Event() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(Event);

  zx_status_t Wait(zx::duration timeout);
  void Signal();
  void Reset();

 private:
  fbl::futex_t signaled_ = {0};
};

// A lightweight wrapper for threads which allow us to create threads and have
// then run a quick lambda, while automating much of the boilerplate we need for
// the ownership tests (things like fetching a thread's KOID)
class Thread {
 public:
  using Thunk = fbl::InlineFunction<int(void), kMaxLambdaStorage>;

  enum class State : uint32_t {
    WAITING_TO_START,
    RUNNING,
    WAITING_TO_STOP,
    STOPPED,
  };

  Thread() { Reset(); }
  DISALLOW_COPY_ASSIGN_AND_MOVE(Thread);

  void Start(const char* name, Thunk thunk);
  zx_status_t Stop();
  zx_status_t GetRunState(uint32_t* run_state) const;

  const zx::thread& handle() const { return handle_; }
  zx_koid_t koid() const { return koid_; }
  State state() const { return state_.load(); }

 private:
  static constexpr zx::duration THREAD_TIMEOUT = kLongTimeout;
  static constexpr zx::duration THREAD_POLL_INTERVAL = zx::msec(1);

  void SetState(State state) { state_.store(state); }

  void Reset();

  thrd_t thread_;
  zx::thread handle_;
  zx_koid_t koid_;
  Event started_evt_;
  Event stop_evt_;
  std::atomic<State> state_{State::STOPPED};
  Thunk thunk_;
};

// A small wrapper used to launch a process which creates a thread and sends us
// a handle to the thread, then waits until we tell it to terminate.  This
// allows us to test the requirement that a process is not allowed to declare a
// thread from another process as the owner of one of its mutexes.
class ExternalThread {
 public:
  static void SetProgramName(const char* program_name) { program_name_ = program_name; }
  static const char* ProgramName() { return program_name_; }

  static int DoHelperThread();
  static const char* helper_flag() { return helper_flag_; }

  ExternalThread() = default;
  ~ExternalThread() { Stop(); }
  DISALLOW_COPY_ASSIGN_AND_MOVE(ExternalThread);

  void Start();
  void Stop();

  const zx::thread& thread() const { return external_thread_; }

 private:
  static const char* program_name_;
  static const char* helper_flag_;

  zx::thread external_thread_;
  zx::channel control_channel_;
};

#endif  // SRC_ZIRCON_TESTS_FUTEX_OWNERSHIP_UTILS_H_
