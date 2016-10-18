// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/tasks/message_loop.h"

#include <magenta/syscalls.h>
#include <mojo/system/result.h>

#include <utility>

#include "lib/ftl/logging.h"

namespace mtl {
namespace {

thread_local MessageLoop* g_current;

constexpr uint32_t kInvalidWaitManyIndexValue = static_cast<uint32_t>(-1);
constexpr MessageLoop::HandlerKey kIgnoredKey = 0;

}  // namespace

MessageLoop::MessageLoop()
    : MessageLoop(ftl::MakeRefCounted<internal::IncomingTaskQueue>()) {}

MessageLoop::MessageLoop(
    ftl::RefPtr<internal::IncomingTaskQueue> incoming_tasks)
    : task_runner_(std::move(incoming_tasks)) {
  FTL_DCHECK(!g_current) << "At most one message loop per thread.";
  event_.reset(mx_event_create(0));
  FTL_CHECK(event_.get() > MX_HANDLE_INVALID);
  MessageLoop::incoming_tasks()->InitDelegate(this);
  g_current = this;
}

MessageLoop::~MessageLoop() {
  FTL_DCHECK(g_current == this)
      << "Message loops must be destroyed on their own threads.";

  // TODO(abarth): What if more handlers are registered here?
  NotifyHandlers(ftl::TimePoint::Max(), MOJO_SYSTEM_RESULT_CANCELLED);

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

MessageLoop::HandlerKey MessageLoop::AddHandler(
    MessageLoopHandler* handler,
    MojoHandle handle,
    MojoHandleSignals handle_signals,
    ftl::TimeDelta timeout) {
  FTL_DCHECK(GetCurrent() == this);
  FTL_DCHECK(handler);
  FTL_DCHECK(handle != MOJO_HANDLE_INVALID);
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

void MessageLoop::NotifyHandlers(ftl::TimePoint now, MojoResult result) {
  // Make a copy in case someone tries to add/remove new handlers as part of
  // notifying.
  const HandleToHandlerData cloned_handlers(handler_data_);
  for (auto it = cloned_handlers.begin(); it != cloned_handlers.end(); ++it) {
    if (it->second.deadline > now)
      continue;
    // Since we're iterating over a clone of the handlers, verify the handler
    // is still valid before notifying.
    if (handler_data_.find(it->first) == handler_data_.end())
      continue;
    handler_data_.erase(it->first);
    it->second.handler->OnHandleError(it->second.handle, result);
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
  MojoDeadline timeout = 0;
  if (next_run_time == ftl::TimePoint::Max()) {
    timeout = MOJO_DEADLINE_INDEFINITE;
  } else if (next_run_time > now) {
    timeout = (next_run_time - now).ToMicroseconds();
  }

  wait_state_.Resize(handler_data_.size() + 1);
  wait_state_.Set(0, kIgnoredKey, event_.get(), MX_SIGNAL_SIGNALED);
  size_t i = 1;
  for (auto it = handler_data_.begin(); it != handler_data_.end(); ++it, ++i) {
    wait_state_.Set(i, it->first, it->second.handle, it->second.signals);
    if (it->second.deadline <= now) {
      timeout = 0;
    } else if (it->second.deadline != ftl::TimePoint::Max()) {
      MojoDeadline handle_timeout =
          (it->second.deadline - now).ToMicroseconds();
      timeout = std::min(timeout, handle_timeout);
    }
  }

  uint32_t result_index = kInvalidWaitManyIndexValue;
  const MojoResult wait_result =
      MojoWaitMany(wait_state_.handles.data(), wait_state_.signals.data(),
                   wait_state_.size(), timeout, &result_index, nullptr);

  // Update now after waiting.
  now = ftl::TimePoint::Now();

  if (result_index == kInvalidWaitManyIndexValue) {
    FTL_DCHECK(wait_result == MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED);
    NotifyHandlers(now, MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED);
    return now;
  }

  if (result_index == 0) {
    FTL_DCHECK(wait_result == MOJO_RESULT_OK);
    mx_status_t event_status =
        mx_object_signal(event_.get(), MX_SIGNAL_SIGNALED, 0u);
    FTL_DCHECK(event_status == NO_ERROR);
    return now;
  }

  HandlerKey key = wait_state_.keys[result_index];
  FTL_DCHECK(handler_data_.find(key) != handler_data_.end());
  const HandlerData& data = handler_data_[key];
  FTL_DCHECK(data.handle == wait_state_.handles[result_index]);
  MessageLoopHandler* handler = handler_data_[key].handler;
  MojoHandle handle = handler_data_[key].handle;

  switch (wait_result) {
    case MOJO_RESULT_OK:
      data.handler->OnHandleReady(handle);
      CallAfterTaskCallback();
      break;
    case MOJO_SYSTEM_RESULT_INVALID_ARGUMENT:
    case MOJO_SYSTEM_RESULT_CANCELLED:
    case MOJO_SYSTEM_RESULT_BUSY:
      // These results indicate a bug in "our" code (e.g., race conditions).
      FTL_DCHECK(false) << "Unexpected wait result: " << wait_result;
    // Fall through.
    case MOJO_SYSTEM_RESULT_FAILED_PRECONDITION:
      // Remove the handle first, this way if OnHandleError() tries to remove
      // the handle our iterator isn't invalidated.
      handler_data_.erase(handle);
      handler->OnHandleError(handle, wait_result);
      CallAfterTaskCallback();
      break;
    default:
      FTL_DCHECK(false) << "Unexpected wait result: " << wait_result;
      break;
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
  mx_status_t status = mx_object_signal(event_.get(), 0u, MX_SIGNAL_SIGNALED);
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
                                 MojoHandle handle,
                                 MojoHandleSignals signals) {
  keys[i] = key;
  handles[i] = handle;
  this->signals[i] = signals;
}

void MessageLoop::WaitState::Resize(size_t size) {
  keys.resize(size);
  handles.resize(size);
  signals.resize(size);
}

}  // namespace mtl
