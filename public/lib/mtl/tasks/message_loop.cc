// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/tasks/message_loop.h"

#include <magenta/syscalls.h>

#include <utility>

#include "lib/ftl/logging.h"

namespace mtl {
namespace {

thread_local MessageLoop* g_current;

constexpr MessageLoop::HandlerKey kIgnoredKey = 0;

}  // namespace

MessageLoop::MessageLoop()
    : MessageLoop(ftl::MakeRefCounted<internal::IncomingTaskQueue>()) {}

MessageLoop::MessageLoop(
    ftl::RefPtr<internal::IncomingTaskQueue> incoming_tasks)
    : task_runner_(std::move(incoming_tasks)) {
  FTL_DCHECK(!g_current) << "At most one message loop per thread.";
  FTL_CHECK(mx::event::create(0, &event_) == NO_ERROR);
  MessageLoop::incoming_tasks()->InitDelegate(this);
  g_current = this;
}

MessageLoop::~MessageLoop() {
  FTL_DCHECK(g_current == this)
      << "Message loops must be destroyed on their own threads.";

  // TODO(jeffbrown): Decide what to do if we couldn't actually clean up
  // all handlers in one pass.  We could retry it a few times but since we
  // are doing this to prevent leaks perhaps it would be less error prone
  // if we changed the ownership model instead.  For example, handlers could
  // be represented as closures which we would simply delete when no longer
  // needed.  (We could still get into loops but it would be less tempting.)
  CancelAllHandlers(ERR_BAD_STATE);
  if (!handler_data_.empty()) {
    FTL_DLOG(WARNING)
        << "MessageLoopHandlers added while destroying the message loop; "
           "they will not be notified of shutdown";
  }

  incoming_tasks()->ClearDelegate();
  ReloadQueue();

  // Destroy the tasks in the order in which they would have run.
  while (!queue_.empty())
    queue_.pop();

  // Finally, remove ourselves from TLS.
  g_current = nullptr;
}

MessageLoop* MessageLoop::GetCurrent() {
  return g_current;
}

MessageLoop::HandlerKey MessageLoop::AddHandler(MessageLoopHandler* handler,
                                                mx_handle_t handle,
                                                mx_signals_t handle_signals,
                                                ftl::TimeDelta timeout) {
  FTL_DCHECK(GetCurrent() == this);
  FTL_DCHECK(handler);
  FTL_DCHECK(handle != MX_HANDLE_INVALID);
  HandlerData handler_data;
  handler_data.handler = handler;
  handler_data.handle = handle;
  handler_data.signals = handle_signals;
  if (timeout == ftl::TimeDelta::Max())
    handler_data.deadline = ftl::TimePoint::Max();
  else
    handler_data.deadline = ftl::TimePoint::Now() + timeout;
  HandlerKey key = next_handler_key_++;
  handler_data_[key] = handler_data;
  return key;
}

void MessageLoop::RemoveHandler(HandlerKey key) {
  FTL_DCHECK(GetCurrent() == this);
  handler_data_.erase(key);
}

bool MessageLoop::HasHandler(HandlerKey key) const {
  return handler_data_.find(key) != handler_data_.end();
}

void MessageLoop::SetAfterTaskCallback(ftl::Closure callback) {
  after_task_callback_ = std::move(callback);
}

void MessageLoop::ClearAfterTaskCallback() {
  after_task_callback_ = ftl::Closure();
}

void MessageLoop::CancelAllHandlers(mx_status_t error) {
  std::vector<HandlerKey> keys;
  for (auto it = handler_data_.begin(); it != handler_data_.end(); ++it) {
    keys.push_back(it->first);
  }
  CancelHandlers(std::move(keys), error);
}

void MessageLoop::CancelHandlers(std::vector<HandlerKey> keys,
                                 mx_status_t error) {
  for (auto key : keys) {
    auto it = handler_data_.find(key);
    if (it == handler_data_.end())
      continue;

    mx_handle_t handle = it->second.handle;
    MessageLoopHandler* handler = it->second.handler;
    handler_data_.erase(it);
    handler->OnHandleError(handle, error);
    CallAfterTaskCallback();
  }
}

void MessageLoop::Run() {
  FTL_DCHECK(!should_quit_);
  FTL_CHECK(!is_running_) << "Cannot run a nested message loop.";
  is_running_ = true;

  ftl::TimePoint now = ftl::TimePoint::Now();
  for (;;) {
    ftl::TimePoint next_run_time = RunReadyTasks(now);
    if (should_quit_)
      break;
    now = ftl::TimePoint::Now();
    now = Wait(now, next_run_time);
    if (should_quit_)
      break;
  }

  should_quit_ = false;

  FTL_DCHECK(is_running_);
  is_running_ = false;
}

ftl::TimePoint MessageLoop::Wait(ftl::TimePoint now,
                                 ftl::TimePoint next_run_time) {
  mx_time_t timeout = 0;
  if (next_run_time == ftl::TimePoint::Max()) {
    timeout = MX_TIME_INFINITE;
  } else if (next_run_time > now) {
    timeout = (next_run_time - now).ToNanoseconds();
  }

  wait_state_.Resize(handler_data_.size() + 1);
  wait_state_.Set(0, kIgnoredKey, event_.get(), MX_SIGNAL_SIGNALED);
  size_t count = 1;
  for (auto it = handler_data_.begin(); it != handler_data_.end();
       ++it, ++count) {
    wait_state_.Set(count, it->first, it->second.handle, it->second.signals);
    if (it->second.deadline <= now) {
      timeout = 0;
    } else if (it->second.deadline != ftl::TimePoint::Max()) {
      mx_time_t handle_timeout = (it->second.deadline - now).ToNanoseconds();
      timeout = std::min(timeout, handle_timeout);
    }
  }

  const mx_status_t wait_status =
      mx_handle_wait_many(wait_state_.items.data(), count, timeout);

  // Update now after waiting.
  now = ftl::TimePoint::Now();

  // Handle errors which indicate bugs in "our" code (e.g. race conditions)
  if (wait_status != NO_ERROR && wait_status != ERR_TIMED_OUT) {
    FTL_DCHECK(false) << "Unexpected wait status: " << wait_status;
    return now;
  }

  // Reset signals on control channel.
  if (wait_state_.items[0].pending & MX_SIGNAL_SIGNALED) {
    mx_status_t rv = event_.signal(MX_SIGNAL_SIGNALED, 0u);
    FTL_DCHECK(rv == NO_ERROR);
  }

  // Deliver pending signals.
  for (size_t i = 1; i < count; ++i) {
    const mx_wait_item_t& wait_item = wait_state_.items[i];
    if (!(wait_item.pending & wait_item.waitfor))
      continue;  // handler was not signalled

    HandlerKey key = wait_state_.keys[i];
    auto it = handler_data_.find(key);
    if (it == handler_data_.end())
      continue;  // handler was removed while delivering signals

    const HandlerData& data = it->second;
    FTL_DCHECK(data.handle == wait_item.handle);
    data.handler->OnHandleReady(data.handle, wait_item.pending);
    CallAfterTaskCallback();
  }

  // Deliver timeouts.
  if (wait_status == ERR_TIMED_OUT) {
    std::vector<HandlerKey> keys;
    for (auto it = handler_data_.begin(); it != handler_data_.end(); ++it) {
      if (it->second.deadline <= now)
        keys.push_back(it->first);
    }
    CancelHandlers(std::move(keys), ERR_TIMED_OUT);
  }

  return now;
}

void MessageLoop::QuitNow() {
  if (is_running_)
    should_quit_ = true;
}

void MessageLoop::PostQuitTask() {
  task_runner()->PostTask([this]() { QuitNow(); });
}

void MessageLoop::ScheduleDrainIncomingTasks() {
  mx_status_t status = event_.signal(0u, MX_SIGNAL_SIGNALED);
  FTL_DCHECK(status == NO_ERROR);
}

bool MessageLoop::RunsTasksOnCurrentThread() {
  return g_current == this;
}

ftl::TimePoint MessageLoop::RunReadyTasks(ftl::TimePoint now) {
  FTL_DCHECK(!should_quit_);
  ReloadQueue();

  while (!queue_.empty() && !should_quit_) {
    ftl::TimePoint next_run_time = queue_.top().target_time();
    if (next_run_time > now)
      return next_run_time;

    internal::PendingTask task =
        std::move(const_cast<internal::PendingTask&>(queue_.top()));
    queue_.pop();

    RunTask(task);
    CallAfterTaskCallback();
  }

  return ftl::TimePoint::Max();
}

void MessageLoop::ReloadQueue() {
  for (auto& task : incoming_tasks()->TakeTaskQueue())
    queue_.push(std::move(task));
}

void MessageLoop::RunTask(const internal::PendingTask& pending_task) {
  const ftl::Closure& closure = pending_task.closure();
  closure();
}

void MessageLoop::CallAfterTaskCallback() {
  if (should_quit_ || !after_task_callback_)
    return;
  after_task_callback_();
}

void MessageLoop::WaitState::Set(size_t i,
                                 HandlerKey key,
                                 mx_handle_t handle,
                                 mx_signals_t signals) {
  keys[i] = key;
  items[i] = {handle, signals, 0};
}

void MessageLoop::WaitState::Resize(size_t size) {
  keys.resize(size);
  items.resize(size);
}

}  // namespace mtl
