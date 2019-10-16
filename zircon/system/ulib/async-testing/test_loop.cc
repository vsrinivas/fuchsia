// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(joshuseaton): Once std lands in Zircon, simplify everything below.

#include <lib/async-testing/test_loop.h>
#include <lib/async-testing/test_loop_dispatcher.h>
#include <lib/async/default.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <utility>

namespace async {
namespace {

// Deterministically updates |m| to point to a pseudo-random number.
void Randomize(uint32_t* m) {
  uint32_t n = *m;
  n ^= (n << 13);
  n ^= (n >> 17);
  n ^= (n << 5);
  *m = n;
}

// Generates a random seed if the environment variable TEST_LOOP_RANDOM_SEED
// is unset; else returns the value of the latter.
uint32_t GetRandomSeed() {
  uint32_t random_seed;
  const char* preset = getenv("TEST_LOOP_RANDOM_SEED");

  if (preset) {
    size_t preset_length = strlen(preset);
    char* endptr = nullptr;
    long unsigned preset_seed = strtoul(preset, &endptr, 10);
    ZX_ASSERT_MSG(preset_seed > 0 && endptr == preset + preset_length,
                  "ERROR: \"%s\" does not give a valid random seed\n", preset);

    random_seed = static_cast<uint32_t>(preset_seed);
  } else {
    zx_cprng_draw(&random_seed, sizeof(uint32_t));
  }

  return random_seed;
}

}  // namespace

class TestLoop::TestSubloop {
 public:
  explicit TestSubloop(async_test_subloop_t* subloop) : subloop_(subloop) {}

  void AdvanceTimeTo(zx::time time) {
    return subloop_->ops->advance_time_to(subloop_.get(), time.get());
  }

  bool DispatchNextDueMessage() { return subloop_->ops->dispatch_next_due_message(subloop_.get()); }

  bool HasPendingWork() { return subloop_->ops->has_pending_work(subloop_.get()); }

  zx::time GetNextTaskDueTime() {
    return zx::time(subloop_->ops->get_next_task_due_time(subloop_.get()));
  }

  async_test_subloop_t* get() { return subloop_.get(); }

 private:
  struct SubloopDeleter {
    void operator()(async_test_subloop_t* loop) { loop->ops->finalize(loop); }
  };

  std::unique_ptr<async_test_subloop_t, SubloopDeleter> subloop_;
};

class TestLoop::TestSubloopToken : public SubloopToken {
 public:
  TestSubloopToken(TestLoop* loop, async_test_subloop_t* subloop)
      : loop_(loop), subloop_(subloop) {}

  ~TestSubloopToken() override {
    auto& subloops = loop_->subloops_;
    for (auto iterator = subloops.begin(); iterator != subloops.end(); iterator++) {
      if (iterator->get() == subloop_) {
        subloops.erase(iterator);
        break;
      }
    }
  }

 private:
  TestLoop* const loop_;
  async_test_subloop_t* subloop_;
};

class TestLoop::TestLoopInterface : public LoopInterface {
 public:
  TestLoopInterface(std::unique_ptr<SubloopToken> token, async_dispatcher_t* dispatcher)
      : token_(std::move(token)), dispatcher_(dispatcher) {}

  ~TestLoopInterface() override = default;

  async_dispatcher_t* dispatcher() override { return dispatcher_; }

 private:
  std::unique_ptr<SubloopToken> token_;
  async_dispatcher_t* dispatcher_;
};

TestLoop::TestLoop() : TestLoop(0) {}

TestLoop::TestLoop(uint32_t state)
    : initial_state_((state != 0) ? state : GetRandomSeed()), state_(initial_state_) {
  default_loop_ = StartNewLoop();
  default_dispatcher_ = default_loop_->dispatcher();
  async_set_default_dispatcher(default_dispatcher_);

  printf("\nTEST_LOOP_RANDOM_SEED=\"%u\"\n", initial_state_);
}

TestLoop::~TestLoop() { async_set_default_dispatcher(nullptr); }

async_dispatcher_t* TestLoop::dispatcher() { return default_dispatcher_; }

bool TestLoop::BlockCurrentSubLoopAndRunOthersUntil(fit::function<bool()> condition) {
  ZX_ASSERT(is_running_);
  ZX_ASSERT(!IsLockedSubLoop(current_subloop_));
  locked_subloops_.push_back(current_subloop_);
  bool success = false;

  // Store initial deadline.
  auto initial_deadline = deadline_;

  // Control advancing time. It is necessary to prevent Run() from advancing the
  // time if |condition()| becomes true in the current run.
  deadline_ = std::min(Now(), initial_deadline);
  while (!success) {
    // Run tasks, which may advance the current time up to |deadline_| but no further.
    bool did_work = Run();

    success = condition();
    if (!did_work) {
      // No work happened and the loop caught up with its deadline, no more
      // event should be handled.
      if (initial_deadline <= Now()) {
        break;
      }
      // Advance the time to the next task due time.
      deadline_ = std::min(GetNextTaskDueTime(), initial_deadline);
    }
  }

  // Restore the initial deadline.
  ZX_ASSERT(deadline_ <= initial_deadline);
  deadline_ = initial_deadline;
  ZX_ASSERT(locked_subloops_.back() == current_subloop_);
  locked_subloops_.pop_back();
  return success;
}

std::unique_ptr<LoopInterface> TestLoop::StartNewLoop() {
  async_dispatcher_t* dispatcher_interface;
  async_test_subloop_t* subloop;
  NewTestLoopDispatcher(&dispatcher_interface, &subloop);
  return std::make_unique<TestLoopInterface>(RegisterLoop(subloop), dispatcher_interface);
}

std::unique_ptr<SubloopToken> TestLoop::RegisterLoop(async_test_subloop_t* subloop) {
  TestSubloop wrapped_subloop{subloop};
  wrapped_subloop.AdvanceTimeTo(Now());
  subloops_.push_back(std::move(wrapped_subloop));
  return std::make_unique<TestSubloopToken>(this, subloop);
}

zx::time TestLoop::Now() const { return current_time_; }

void TestLoop::Quit() { has_quit_ = true; }

void TestLoop::AdvanceTimeByEpsilon() { AdvanceTimeTo(Now() + zx::duration(1)); }

bool TestLoop::RunUntil(zx::time deadline) {
  ZX_ASSERT(!is_running_);
  is_running_ = true;
  deadline_ = deadline;
  bool did_work = Run();
  has_quit_ = false;
  is_running_ = false;
  return did_work;
}

bool TestLoop::RunFor(zx::duration duration) { return RunUntil(Now() + duration); }

bool TestLoop::RunUntilIdle() { return RunUntil(Now()); }

bool TestLoop::HasPendingWork() {
  for (auto& subloop : subloops_) {
    if (IsLockedSubLoop(&subloop)) {
      continue;
    }
    if (subloop.HasPendingWork()) {
      return true;
    }
  }
  return false;
}

zx::time TestLoop::GetNextTaskDueTime() {
  zx::time next_due_time = zx::time::infinite();
  for (auto& subloop : subloops_) {
    if (IsLockedSubLoop(&subloop)) {
      continue;
    }
    next_due_time = std::min<zx::time>(next_due_time, subloop.GetNextTaskDueTime());
  }
  return next_due_time;
}

void TestLoop::AdvanceTimeTo(zx::time time) {
  if (current_time_ < time) {
    current_time_ = time;
    for (auto& subloop : subloops_) {
      subloop.AdvanceTimeTo(time);
    }
  }
}

bool TestLoop::IsLockedSubLoop(TestSubloop* subloop) {
  return std::find(locked_subloops_.begin(), locked_subloops_.end(), subloop) !=
         locked_subloops_.end();
}

bool TestLoop::Run() {
  TestSubloop* initial_loop = current_subloop_;
  bool did_work = false;
  while (!has_quit_ || !locked_subloops_.empty()) {
    if (!HasPendingWork()) {
      zx::time next_due_time = GetNextTaskDueTime();
      if (next_due_time > deadline_) {
        AdvanceTimeTo(deadline_);
        break;
      }
      AdvanceTimeTo(next_due_time);
    }

    Randomize(&state_);
    size_t current_index = state_ % subloops_.size();
    current_subloop_ = &subloops_[current_index];
    if (IsLockedSubLoop(current_subloop_)) {
      if (current_subloop_ == initial_loop) {
        did_work = true;
        break;
      }
      continue;
    }

    did_work |= current_subloop_->DispatchNextDueMessage();
  }
  current_subloop_ = initial_loop;
  return did_work;
}

}  // namespace async
