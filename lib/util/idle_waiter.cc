// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/util/idle_waiter.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fxl/functional/closure.h>

namespace util {

IdleWaiter::Activity::Activity(fxl::WeakPtr<IdleWaiter> tracker)
    : tracker_(tracker) {
  tracker_->activity_ = this;
}

IdleWaiter::Activity::~Activity() {
  if (tracker_) {
    tracker_->activity_ = nullptr;
    tracker_->PostIdleCheck();
  }
}

IdleWaiter::IdleWaiter() : weak_ptr_factory_(this) {}

IdleWaiter::~IdleWaiter() = default;

void IdleWaiter::SetLoop(async::Loop* loop) {
  FXL_DCHECK(!loop_);
  loop_ = loop;
}

IdleWaiter::ActivityToken IdleWaiter::RegisterOngoingActivity() {
  FXL_DCHECK(loop_->dispatcher() == async_get_default_dispatcher());

  if (activity_) {
    return ActivityToken(activity_);
  } else {
    // |activity_| is set in the |Activity| constructor and cleared in
    // the destructor
    return fxl::MakeRefCounted<Activity>(weak_ptr_factory_.GetWeakPtr());
  }
}

void IdleWaiter::WaitUntilIdle(fxl::Closure callback) {
  callbacks_.push_back(std::move(callback));
  PostIdleCheck();
}

void IdleWaiter::PostIdleCheck() {
  if (!(callbacks_.empty() || activity_ || idle_check_pending_)) {
    FXL_DCHECK(loop_->dispatcher() == async_get_default_dispatcher());
    loop_->Quit();
    idle_check_pending_ = true;
  }
}

bool IdleWaiter::FinishIdleCheck() {
  if (idle_check_pending_) {
    FXL_DCHECK(loop_->dispatcher() == async_get_default_dispatcher());
    loop_->RunUntilIdle();
    loop_->ResetQuit();
    if (!activity_) {
      for (const auto& callback : callbacks_) {
        callback();
      }
      callbacks_.clear();
    }
    // Otherwise, |PostIdleCheck| will be invoked again when |activity_| is
    // released.

    idle_check_pending_ = false;
    return true;
  } else {
    return false;
  }
}

}  // namespace util
