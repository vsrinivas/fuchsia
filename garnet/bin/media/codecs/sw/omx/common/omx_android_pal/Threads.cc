// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils/Thread.h"

namespace android {

Thread::Thread(bool can_call_java) {
  // Fuchsia android::Thread shim doesn't support can_call_java == true.
  assert(!can_call_java);
}

Thread::~Thread() {
  bool is_join_needed = false;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!is_run_called_) {
      // Nothing started, so nothing to shut down.
      return;
    }
    // run() can't fail in any way other than calling abort() on the process.
    assert(thread_);
    // The caller _must_ have at least requested that the thread stop by this
    // point, by calling requestExit() or requestExitAndWait(), or by returning
    // false from threadLoop(), or by returning a failing status from
    // readyToRun(), or by dropping the strong refcount to 0.
    assert(is_exit_requested_);
    // If the current thread is this thread, then don't try to wait for this
    // thread to exit.
    if (std::this_thread::get_id() == thread_->get_id()) {
      thread_->detach();
      // This usage pattern isn't necessarily consistent with safe un-load of a
      // shared library.  For the Fuchsia scenarios involving this code (for
      // now), we don't currently care about code un-load safety but we may in
      // future (in those same scenarios), so we assert in this case for now,
      // since we don't expect this case to get hit in the first place.
      assert(false);
      // ~thread_ here will be able to ~std::thread successfully because of the
      // std::thread::detach() above.
      return;
    }
    if (!is_joiner_selected_) {
      is_join_needed = true;
      is_joiner_selected_ = true;
    } else {
      // Some other thread was selected as the joiner.  That means that other
      // thread started running requestExitAndWait().  If that other thread is
      // _still_ running requestExitAndWait(), that's a bug in calling code,
      // because that code shouldn't race Thread::requestExitAndWait() with
      // Thread::~Thread().
      //
      // I suppose another way to end up here would be for two threads to both
      // call ~Thread() on teh same Thread instance, which would of course also
      // be a bug in calling code.  Note that this assert only potentially
      // detects one sub-case of that bug - the sub-case where the thread isn't
      // joined yet.
      assert(is_joined_);
    }
  }

  if (is_join_needed) {
    joinCommon();
  }

  // Definitely true by this ponit.  Don't bother acquiring the lock for this
  // check as once this becomes true it stays true, and we already had the lock
  // enough above to make lock holding for this check unnecessary.
  assert(is_joined_);

  // Now it's safe to delete the std::thread, which will happen during ~thread_
  // implicitly here.
}

status_t Thread::readyToRun() { return NO_ERROR; }

status_t Thread::run(const char* thread_name, int32_t thread_priority,
                     size_t stack_size) {
  assert(thread_name);
  std::unique_lock<std::mutex> lock(lock_);
  if (is_run_called_) {
    return INVALID_OPERATION;
  }
  is_run_called_ = true;
  // hold strong reference on self until _threadLoop() gets going
  hold_self_ = this;
  thread_ = std::make_unique<std::thread>([this] { _threadLoop(); });
  // Can't touch this.
  return NO_ERROR;
}

void Thread::_threadLoop() {
  sp<Thread> strong(hold_self_);
  wp<Thread> weak(strong);
  hold_self_.clear();
  bool first = true;
  do {
    bool is_wanting_to_run;
    if (first) {
      first = false;
      start_status_ = readyToRun();
      is_wanting_to_run = (start_status_ == NO_ERROR);
      if (is_wanting_to_run && !isExitRequested()) {
        is_wanting_to_run = threadLoop();
      }
    } else {
      is_wanting_to_run = threadLoop();
    }

    {  // scope lock
      std::unique_lock<std::mutex> lock(lock_);
      if (!is_wanting_to_run) {
        is_exit_requested_ = true;
      }
      if (is_exit_requested_) {
        // We don't try to self-report that this thread is done, because this
        // thread isn't done running code of this method until the "ret"
        // instruction at the end of this method (or equivalent) is over, so the
        // only safe way to know that this thread is done running code of this
        // method is to use OS-provided mechanism to determine that this thread
        // is really done running, which std::thread.join() does do (or at
        // least, certainly should do).
        break;
      }
    }  // ~lock

    strong.clear();
    strong = weak.promote();
    if (strong == nullptr) {
      std::unique_lock<std::mutex> lock(lock_);
      // It's nice to treat the strong refcount dropping to zero as an official
      // exit request, before ~wp.
      is_exit_requested_ = true;
    }
  } while (strong != nullptr);
  // ~wp can be how this gets deleted, but for now we asser in the destructor
  // if ~wp calls delete this, because that usage pattern isn't consistent with
  // safe un-load of the code of a shared library.
}

void Thread::requestExit() {
  std::unique_lock<std::mutex> lock(lock_);
  is_exit_requested_ = true;
}

status_t Thread::requestExitAndWait() {
  bool is_join_needed = false;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!is_run_called_) {
      return NO_ERROR;
    }
    if (thread_->get_id() == std::this_thread::get_id()) {
      return WOULD_BLOCK;
    }
    if (!is_exit_requested_) {
      is_exit_requested_ = true;
      is_join_needed = true;
      is_joiner_selected_ = true;
    } else {
      if (!is_joiner_selected_) {
        is_join_needed = true;
        is_joiner_selected_ = true;
      }
    }
    // Even if this thread isn't the one selected to do the join, this thread
    // still has to wait for the join to be done.
    if (!is_join_needed) {
      while (!is_joined_) {
        joined_condition_.wait(lock);
      }
      return NO_ERROR;
    }
  }  // ~lock

  assert(is_join_needed);
  joinCommon();

  return start_status_;
}

bool Thread::isExitRequested() const {
  std::unique_lock<std::mutex> lock(lock_);
  return is_exit_requested_;
}

void Thread::joinCommon() {
  thread_->join();
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    is_joined_ = true;
  }  // ~lock
  joined_condition_.notify_all();
}

}  // namespace android
