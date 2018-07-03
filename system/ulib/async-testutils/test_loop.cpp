// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/test_loop.h>

#include <fbl/intrusive_wavl_tree.h>
#include <lib/async-testutils/time-keeper.h>
#include <lib/async/default.h>

namespace async {
namespace {

// Timer abstractions to be 'fired' as time is advanced.
class Timer : public fbl::WAVLTreeContainable<fbl::unique_ptr<Timer>> {
public:
    Timer(zx::time deadline, TimerDispatcher* dispatcher)
        : deadline_(deadline), dispatcher_(dispatcher) {}

    zx::time Deadline() const { return deadline_; }

    bool IsDispatchedBy(TimerDispatcher* dispatcher) const {
        return dispatcher_ == dispatcher;
    }

    void Fire() { dispatcher_->FireTimer(); }

    // Trait implementation for fbl::WAVLTree.
    zx::time GetKey() const { return deadline_; }

private:
    const zx::time deadline_;
    TimerDispatcher* const dispatcher_;
};

} // namespace

class TestLoop::TestLoopTimeKeeper : public TimeKeeper {
public:
    TestLoopTimeKeeper() = default;
    ~TestLoopTimeKeeper() = default;

    zx::time Now() const override { return current_time_; }

    void RegisterTimer(zx::time deadline, TimerDispatcher* timer_dispatcher) override {
        // If |deadline| has passed, signal expiration immediately.
        if (deadline <= current_time_) {
            timer_dispatcher->FireTimer();
            return;
        }
        fbl::unique_ptr<Timer> fake_timer(new Timer(deadline, timer_dispatcher));
        fake_timers_.insert_or_find(fbl::move(fake_timer));
    }

    void CancelTimers(TimerDispatcher* timer_dispatcher) override {
      auto iter = fake_timers_.begin();
      while (iter.IsValid()) {
          auto current = iter++;
          if (current->IsDispatchedBy(timer_dispatcher)) {
              fake_timers_.erase(current);
          }
      }
    }

    void AdvanceTimeTo(zx::time time) {
        if (time < current_time_) { return; }
        current_time_ = time;
        while (!fake_timers_.is_empty() && fake_timers_.front().Deadline() <= current_time_) {
            fake_timers_.front().Fire();
            fake_timers_.pop_front();
        }
    }

private:
    zx::time current_time_;
    fbl::WAVLTree<zx::time, fbl::unique_ptr<Timer>> fake_timers_;
};

TestLoop::TestLoop()
    : time_keeper_(new TestLoopTimeKeeper()), dispatcher_(time_keeper_.get()) {
    async_set_default_dispatcher(&dispatcher_);
}

TestLoop::~TestLoop() {
    async_set_default_dispatcher(nullptr);
}

async_dispatcher_t* TestLoop::dispatcher() {
    return &dispatcher_;
}

async_dispatcher_t* TestLoop::async() {
    return dispatcher();
}

zx::time TestLoop::Now() {
    return time_keeper_->Now();
}

void TestLoop::AdvanceTimeTo(zx::time time) {
    time_keeper_->AdvanceTimeTo(time);
}

void TestLoop::AdvanceTimeBy(zx::duration delta) {
    AdvanceTimeTo(Now() + delta);
}

void TestLoop::Quit() {
    has_quit_ = true;
}

bool TestLoop::RunUntil(zx::time deadline) {
    ZX_ASSERT(!is_running_);
    is_running_ = true;
    bool did_work = false;
    for (;;) {
        bool ran_handler = dispatcher_.DispatchNextDueMessage();
        did_work |= ran_handler;

        if (has_quit_) { break; }
        if (ran_handler) { continue; }

        zx::time next_due_time = dispatcher_.GetNextTaskDueTime();
        if (next_due_time > deadline) {
            AdvanceTimeTo(deadline);
            break;
        }
        AdvanceTimeTo(next_due_time);
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

} // namespace async
