// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/message_loop.h"

#include <algorithm>

#include "src/developer/debug/shared/logging/block_timer.h"
#include "src/lib/fxl/logging.h"

namespace debug_ipc {

namespace {

thread_local MessageLoop* current_message_loop = nullptr;

}  // namespace

MessageLoop::MessageLoop() = default;

MessageLoop::~MessageLoop() {
  FXL_DCHECK(Current() != this);  // Cleanup() should have been called.
}

void MessageLoop::Init() {
  FXL_DCHECK(!current_message_loop);
  current_message_loop = this;
}

void MessageLoop::Cleanup() {
  FXL_DCHECK(current_message_loop == this);
  current_message_loop = nullptr;
}

// static
MessageLoop* MessageLoop::Current() { return current_message_loop; }

void MessageLoop::Run() {
  should_quit_ = false;
  RunImpl();
}

void MessageLoop::PostTask(FileLineFunction file_line,
                           std::function<void()> fn) {
  bool needs_awaken;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    needs_awaken = task_queue_.empty();
    task_queue_.push_back({std::move(file_line), std::move(fn)});
  }
  if (needs_awaken)
    SetHasTasks();
}

void MessageLoop::PostTimer(FileLineFunction file_line, uint64_t delta_ms,
                            std::function<void()> fn) {
  constexpr uint64_t kMsToNs = 1000000;

  bool needs_awaken;
  uint64_t expiry = delta_ms * kMsToNs + GetMonotonicNowNS();

  {
    std::lock_guard<std::mutex> guard(mutex_);
    needs_awaken = task_queue_.empty() && NextExpiryNS() > expiry;
    timers_.push_back({{std::move(file_line), std::move(fn)}, expiry});
    std::push_heap(timers_.begin(), timers_.end(), &CompareTimers);
  }
  if (needs_awaken)
    SetHasTasks();
}

uint64_t MessageLoop::DelayNS() const {
  // NextExpiry will return kMaxDelay if there are no timers queued.
  uint64_t expiry = NextExpiryNS();
  if (expiry == kMaxDelay) {
    return kMaxDelay;
  }

  // We check how much more time we need to wait.
  uint64_t now = GetMonotonicNowNS();
  if (expiry > now) {
    return expiry - now;
  }

  return 0;
}

uint64_t MessageLoop::NextExpiryNS() const {
  if (timers_.empty()) {
    return kMaxDelay;
  }

  return timers_[0].expiry;
}

void MessageLoop::QuitNow() { should_quit_ = true; }

bool MessageLoop::ProcessPendingTask() {
  // This function will be called with the mutex held.
  if (task_queue_.empty() && DelayNS() > 0) {
    return false;
  }

  Task task;
  if (!task_queue_.empty()) {
    task = std::move(task_queue_.front());
    task_queue_.pop_front();
  } else {
    std::pop_heap(timers_.begin(), timers_.end(), &CompareTimers);
    task = std::move(timers_.back().task);
    timers_.pop_back();
  }

  {
    mutex_.unlock();
    {
      debug_ipc::BlockTimer(task.file_line);
      task.task_fn();
    }
    mutex_.lock();
  }
  return true;
}

MessageLoop::WatchHandle::WatchHandle() = default;
MessageLoop::WatchHandle::WatchHandle(MessageLoop* msg_loop, int id)
    : msg_loop_(msg_loop), id_(id) {}

MessageLoop::WatchHandle::WatchHandle(WatchHandle&& other)
    : msg_loop_(other.msg_loop_), id_(other.id_) {
  other.msg_loop_ = nullptr;
  other.id_ = 0;
}

MessageLoop::WatchHandle::~WatchHandle() { StopWatching(); }

void MessageLoop::WatchHandle::StopWatching() {
  if (watching())
    msg_loop_->StopWatching(id_);
  msg_loop_ = nullptr;
  id_ = 0;
}

MessageLoop::WatchHandle& MessageLoop::WatchHandle::operator=(
    WatchHandle&& other) {
  // Should never get into a self-assignment situation since this is not
  // copyable and every ID should be unique. Do allow self-assignment of
  // null ones though.
  FXL_DCHECK(!watching() || (msg_loop_ != other.msg_loop_ || id_ != other.id_));
  if (watching())
    msg_loop_->StopWatching(id_);
  msg_loop_ = other.msg_loop_;
  id_ = other.id_;

  other.msg_loop_ = nullptr;
  other.id_ = 0;
  return *this;
}

}  // namespace debug_ipc
