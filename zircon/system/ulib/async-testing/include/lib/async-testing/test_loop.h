// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_TESTING_TEST_LOOP_H_
#define LIB_ASYNC_TESTING_TEST_LOOP_H_

#include <lib/async-testing/test_subloop.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <memory>
#include <vector>

namespace async {

// A minimal, abstract async dispatcher-based message loop interface.
class LoopInterface {
 public:
  virtual ~LoopInterface() = default;
  virtual async_dispatcher_t* dispatcher() = 0;
};

// A registration token for a subloop of the test loop.
class SubloopToken {
 public:
  virtual ~SubloopToken() = default;
};

// A message loop with a fake clock, to be controlled within a test setting.
class TestLoop final {
 public:
  // Constructs a TestLoop with a seed from the environment, or a random
  // seed if absent.
  TestLoop();
  // If state is nonzero, constructs a TestLoop with the given seed.
  // Otherwise, uses a seed from the environment or a random seed.
  explicit TestLoop(uint32_t state);
  ~TestLoop();

  TestLoop(const TestLoop&) = delete;
  TestLoop& operator=(const TestLoop&) = delete;

  TestLoop(TestLoop&&) = delete;
  TestLoop& operator=(TestLoop&&) = delete;

  // Returns the test loop's asynchronous dispatcher.
  async_dispatcher_t* dispatcher();

  // Returns a loop interface simulating the starting up of a new message
  // loop. Each successive call to this method corresponds to a new
  // subloop. The subloop is unregistered and destructed when the returned
  // interface is destructed. The returned interface must not outlive the test
  // loop.
  std::unique_ptr<LoopInterface> StartNewLoop();

  // Registers a new loop. The test loop takes ownership of the subloop. The
  // subloop is unregistered and finalized when the returned registration
  // token is destructed. The token must not outlive the test loop.
  std::unique_ptr<SubloopToken> RegisterLoop(async_test_subloop_t* loop);

  // Returns the current fake clock time.
  zx::time Now() const;

  // Quits the message loop. If called while running, it will immediately
  // exit and dispatch no further tasks or waits; if called before running,
  // then next call to run will immediately exit. Further calls to run will
  // dispatch as usual.
  void Quit();

  // This method must be called while running. It will block the current subloop
  // until |condition| is realized. Other subloops will continue to run. Returns
  // |true| when |condition| is realized, and |false| if |condition| is not
  // realized and no further progress is possible.
  bool BlockCurrentSubLoopAndRunOthersUntil(fit::function<bool()> condition);

  // Advances the fake clock time by the smallest possible amount.
  // This doesn't run the loop.
  void AdvanceTimeByEpsilon();

  // Dispatches all waits and all tasks with deadlines up until |deadline|,
  // progressively advancing the fake clock.
  // Returns true iff any tasks or waits were invoked during the run.
  bool RunUntil(zx::time deadline);

  // Dispatches all waits and all tasks with deadlines up until |duration|
  // from the the current time, progressively advancing the fake clock.
  // Returns true iff any tasks or waits were invoked during the run.
  bool RunFor(zx::duration duration);

  // Dispatches all waits and all tasks with deadlines up until the current
  // time, progressively advancing the fake clock.
  // Returns true iff any tasks or waits were invoked during the run.
  bool RunUntilIdle();

  // The initial value of the state of the TestLoop.
  uint32_t initial_state() { return initial_state_; }

 private:
  // An implementation of LoopInterface.
  class TestLoopInterface;

  // An implementation of LoopToken.
  class TestSubloopToken;

  // Wraps a subloop in a friendly interface.
  class TestSubloop;

  // Whether there are any due tasks or waits across |dispatchers_|.
  bool HasPendingWork();

  // Returns the next due task time across |dispatchers_|.
  zx::time GetNextTaskDueTime();

  // Advances the time to |time| and notifies the subloops.
  void AdvanceTimeTo(zx::time time);

  // Returns whether the given subloop is locked.
  bool IsLockedSubLoop(TestSubloop* subloop);

  // Runs the loop until either:
  // - The loop quit method is called.
  // - No unlocked subloop has any available task.
  // - An event on the current loop must be run when the current loop is locked.
  //
  // This method returns |true| if an event has been dispatched while running,
  // or some event could be run but the method returned due to trying
  // dispatching an event on the current locked loop.
  // |current_subloop_| is guaranteed to be unchanged when this method returns.
  bool Run();

  // The current time. Invariant: all subloops have been notified of the
  // current time.
  zx::time current_time_;

  // The interface to the loop associated with the default async dispatcher.
  std::unique_ptr<LoopInterface> default_loop_;

  // The default async dispatcher.
  async_dispatcher_t* default_dispatcher_;

  // The dispatchers running in this test loop.
  std::vector<TestSubloop> subloops_;

  // The subloop dispatching the currently run event.
  TestSubloop* current_subloop_ = nullptr;

  // The set of subloop currently blocked on |BlockCurrentSubLoopAndRunOthersUntil|.
  std::vector<TestSubloop*> locked_subloops_;

  // The seed of a pseudo-random number used to determinisitically determine the
  // dispatching order across |dispatchers_|.
  uint32_t initial_state_;
  // The current state of the pseudo-random generator.
  uint32_t state_;

  // The deadline of the current run of the loop.
  zx::time deadline_;
  // Quit state of the loop.
  bool has_quit_ = false;
  // Whether the loop is currently running.
  bool is_running_ = false;
};

}  // namespace async

#endif  // LIB_ASYNC_TESTING_TEST_LOOP_H_
