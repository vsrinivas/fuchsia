// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/util/event_timestamper.h"

#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"

namespace scene_manager {

EventTimestamper::EventTimestamper()
    : main_loop_(mtl::MessageLoop::GetCurrent()) {
  FTL_DCHECK(main_loop_);
  background_loop_.StartThread();
  IncreaseBackgroundThreadPriority();
}

EventTimestamper::~EventTimestamper() {
  background_loop_.Shutdown();
#ifndef NDEBUG
  FTL_CHECK(watch_count_ == 0);
#endif
}

void EventTimestamper::IncreaseBackgroundThreadPriority() {
  Post(background_loop_.async());
}

async_task_result_t EventTimestamper::Handle(async_t* async,
                                             mx_status_t status) {
  mx_thread_set_priority(24 /* HIGH_PRIORITY in LK */);
  return ASYNC_TASK_FINISHED;
}

EventTimestamper::Watch::Watch() : wait_(nullptr), timestamper_(nullptr) {}

EventTimestamper::Watch::Watch(EventTimestamper* ts,
                               mx::event event,
                               mx_status_t trigger,
                               Callback callback)
    : wait_(new EventTimestamper::Wait(ts->main_loop_->task_runner(),
                                       std::move(event),
                                       trigger,
                                       std::move(callback))),
      timestamper_(ts) {
  FTL_DCHECK(timestamper_);
#ifndef NDEBUG
  ++timestamper_->watch_count_;
#endif
}

EventTimestamper::Watch::Watch(Watch&& other)
    : wait_(other.wait_), timestamper_(other.timestamper_) {
  other.wait_ = nullptr;
  other.timestamper_ = nullptr;
}

EventTimestamper::Watch::~Watch() {
  if (!wait_) {
    // Was moved.
    return;
  }
#ifndef NDEBUG
  --timestamper_->watch_count_;
#endif

  switch (wait_->state()) {
    case Wait::State::STOPPED:
      delete wait_;
      break;
    case Wait::State::STARTED:
      if (MX_OK == wait_->Cancel(timestamper_->background_loop_.async())) {
        delete wait_;
      } else {
        wait_->set_state(Wait::State::ABANDONED);
      }
      break;
    case Wait::State::ABANDONED:
      FTL_DCHECK(false) << "internal error.";
      break;
  }
}

void EventTimestamper::Watch::Start() {
  FTL_DCHECK(wait_) << "invalid Watch (was it std::move()d?).";
  FTL_DCHECK(wait_->state() == Wait::State::STOPPED)
      << "illegal to call Start() again before callback has been received.";
  wait_->set_state(Wait::State::STARTED);
  wait_->Begin(timestamper_->background_loop_.async());
}

EventTimestamper::Wait::Wait(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
                             mx::event event,
                             mx_status_t trigger,
                             Callback callback)
    : async::Wait(event.get(), trigger),
      task_runner_(task_runner),
      event_(std::move(event)),
      callback_(std::move(callback)) {}

EventTimestamper::Wait::~Wait() {
  FTL_DCHECK(state_ == State::STOPPED || state_ == State::ABANDONED);
}

async_wait_result_t EventTimestamper::Wait::Handle(
    async_t* async,
    mx_status_t status,
    const mx_packet_signal_t* signal) {
  mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
  task_runner_->PostTask([now, this] {
    if (state_ == State::ABANDONED) {
      // The EventTimestamper::Watch that owned us was destroyed; we must
      // immediately destroy ourself or our memory will be leaked.
      delete this;
      return;
    }
    FTL_DCHECK(state_ == State::STARTED) << "internal error.";
    state_ = State::STOPPED;
    callback_(now);
  });

  return ASYNC_WAIT_FINISHED;
}

}  // namespace scene_manager
