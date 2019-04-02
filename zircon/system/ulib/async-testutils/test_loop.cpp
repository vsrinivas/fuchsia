// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(joshuseaton): Once std lands in Zircon, simplify everything below.

#include <lib/async-testutils/test_loop.h>

#include <stdlib.h>

#include <fbl/algorithm.h>
#include <lib/async-testutils/time-keeper.h>
#include <lib/async/default.h>
#include <lib/zircon-internal/xorshiftrand.h>
#include <zircon/syscalls.h>

#include <utility>

namespace async {
namespace {

// Determinisitically updates |m| to point to a pseudo-random number.
void Randomize(uint32_t* m) {
  rand32_t r = { .n = *m };
  *m = rand32(&r);
}

// Generates a random seed if the environment variable TEST_LOOP_RANDOM_SEED
// is unset; else returns the value of the latter.
uint32_t GetRandomSeed() {;
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

} // namespace

class TestLoop::TestLoopTimeKeeper : public TimeKeeper {
public:
    TestLoopTimeKeeper() = default;
    ~TestLoopTimeKeeper() = default;

    zx::time Now() const override { return current_time_; }

    void AdvanceTimeTo(zx::time time) {
        if (time < current_time_) { return; }
        current_time_ = time;
    }

private:
    zx::time current_time_;
};


class TestLoop::TestLoopInterface : public LoopInterface {
public:
      TestLoopInterface(TestLoop* loop, TestLoopDispatcher* dispatcher)
          : loop_(loop), dispatcher_(dispatcher) {}

      ~TestLoopInterface() override {
          auto& dispatchers = loop_->dispatchers_;
          for (size_t index = 0; index < dispatchers.size(); ++index) {
              if (dispatchers[index].get() == dispatcher_) {
                  dispatchers.erase(index);
                  break;
              }
          }
          dispatcher_ = nullptr;
      }

      async_dispatcher_t* dispatcher() override { return dispatcher_; }

private:
    TestLoop* const loop_;
    TestLoopDispatcher* dispatcher_;
};

TestLoop::TestLoop() : TestLoop(0) {}

TestLoop::TestLoop(uint32_t state)
    : time_keeper_(new TestLoopTimeKeeper()),
      initial_state_((state != 0)? state : GetRandomSeed()), state_(initial_state_) {
    dispatchers_.push_back(fbl::make_unique<TestLoopDispatcher>(time_keeper_.get()));
    async_set_default_dispatcher(dispatchers_[0].get());

    printf("\nTEST_LOOP_RANDOM_SEED=\"%u\"\n", initial_state_);
}

TestLoop::~TestLoop() {
    async_set_default_dispatcher(nullptr);
}

async_dispatcher_t* TestLoop::dispatcher() {
    return dispatchers_[0].get();
}

fbl::unique_ptr<LoopInterface> TestLoop::StartNewLoop() {
    dispatchers_.push_back(fbl::make_unique<TestLoopDispatcher>(time_keeper_.get()));
    auto* new_dispatcher = dispatchers_[dispatchers_.size() - 1].get();
    return fbl::make_unique<TestLoopInterface>(this, new_dispatcher);
}

zx::time TestLoop::Now() const {
    return time_keeper_->Now();
}

void TestLoop::Quit() {
    has_quit_ = true;
}


void TestLoop::AdvanceTimeByEpsilon() {
    time_keeper_->AdvanceTimeTo(Now() + zx::duration(1));
}

bool TestLoop::RunUntil(zx::time deadline) {
    ZX_ASSERT(!is_running_);
    is_running_ = true;
    bool did_work = false;
    while (!has_quit_) {
        if (!HasPendingWork()) {
            zx::time next_due_time = GetNextTaskDueTime();
            if (next_due_time > deadline) {
                time_keeper_->AdvanceTimeTo(deadline);
                break;
            }
            time_keeper_->AdvanceTimeTo(next_due_time);
        }

        Randomize(&state_);
        size_t current_index = state_ % dispatchers_.size();
        auto& current_dispatcher = dispatchers_[current_index];

        async_set_default_dispatcher(current_dispatcher.get());
        did_work |= current_dispatcher->DispatchNextDueMessage();
        async_set_default_dispatcher(dispatchers_[0].get());

    }
    is_running_ = false;
    has_quit_ = false;
    return did_work;
}

bool TestLoop::RunFor(zx::duration duration) {
    return RunUntil(Now() + duration);
}

bool TestLoop::RunUntilIdle() {
    return RunUntil(Now());
}

bool TestLoop::HasPendingWork() {
    for (auto& dispatcher : dispatchers_) {
        if (dispatcher->HasPendingWork()) { return true; }
    }
    return false;
}

zx::time TestLoop::GetNextTaskDueTime() const {
  zx::time next_due_time = zx::time::infinite();
  for (auto& dispatcher : dispatchers_) {
      next_due_time =
          fbl::min<zx::time>(next_due_time, dispatcher->GetNextTaskDueTime());
  }
  return next_due_time;
}

} // namespace async
