// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/message_loop.h"

#include "garnet/public/lib/fxl/logging.h"

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

void MessageLoop::PostTask(std::function<void()> fn) {
  bool needs_awaken;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    needs_awaken = task_queue_.empty();
    task_queue_.push_back(std::move(fn));
  }
  if (needs_awaken)
    SetHasTasks();
}

void MessageLoop::QuitNow() { should_quit_ = true; }

bool MessageLoop::ProcessPendingTask() {
  // This function will be called with the mutex held.
  if (task_queue_.empty())
    return false;

  std::function<void()> task = std::move(task_queue_.front());
  task_queue_.pop_front();
  {
    mutex_.unlock();
    task();
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

MessageLoop::WatchHandle::~WatchHandle() {
  if (watching())
    msg_loop_->StopWatching(id_);
}

MessageLoop::WatchHandle& MessageLoop::WatchHandle::operator=(
    WatchHandle&& other) {
  // Should never get into a self-assignment situation since this is not
  // copyable and every ID should be unique.
  FXL_DCHECK(msg_loop_ != other.msg_loop_ || id_ != other.id_);
  if (watching())
    msg_loop_->StopWatching(id_);
  msg_loop_ = other.msg_loop_;
  id_ = other.id_;

  other.msg_loop_ = nullptr;
  other.id_ = 0;
  return *this;
}

}  // namespace debug_ipc
