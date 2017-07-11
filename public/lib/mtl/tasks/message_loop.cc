// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/tasks/message_loop.h"

#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>
#include <mx/time.h>

#include <utility>

#include "lib/ftl/logging.h"

namespace mtl {
namespace {

thread_local MessageLoop* g_current;

constexpr MessageLoop::HandlerKey kDrainKey = 0;

}  // namespace

MessageLoop::MessageLoop()
    : MessageLoop(ftl::MakeRefCounted<internal::IncomingTaskQueue>()) {}

MessageLoop::MessageLoop(
    ftl::RefPtr<internal::IncomingTaskQueue> incoming_tasks)
    : task_runner_(std::move(incoming_tasks)) {
  FTL_DCHECK(!g_current) << "At most one message loop per thread.";
  FTL_CHECK(mx::port::create(0, &port_) == MX_OK);
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
  CancelAllHandlers(MX_ERR_BAD_STATE);
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
                                                mx_signals_t trigger,
                                                ftl::TimeDelta timeout) {
  FTL_DCHECK(GetCurrent() == this);
  FTL_DCHECK(handler);
  FTL_DCHECK(handle != MX_HANDLE_INVALID);
  HandlerData handler_data;
  handler_data.handler = handler;
  handler_data.handle = handle;
  handler_data.trigger = trigger;
  if (timeout == ftl::TimeDelta::Max())
    handler_data.deadline = ftl::TimePoint::Max();
  else
    handler_data.deadline = ftl::TimePoint::Now() + timeout;
  HandlerKey key = next_handler_key_++;
  handler_data_[key] = handler_data;
  mx_object_wait_async(handle, port_.get(), key, trigger, MX_WAIT_ASYNC_ONCE);
  return key;
}

void MessageLoop::RemoveHandler(HandlerKey key) {
  FTL_DCHECK(GetCurrent() == this);
  auto it = handler_data_.find(key);
  if (it == handler_data_.end())
    return;
  port_.cancel(it->second.handle, key);
  handler_data_.erase(it);
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
  for (auto it = handler_data_.begin(); it != handler_data_.end(); ++it)
    keys.push_back(it->first);
  CancelHandlers(std::move(keys), error);
}

void MessageLoop::CancelHandlers(std::vector<HandlerKey> keys,
                                 mx_status_t error) {
  for (auto key : keys) {
    auto it = handler_data_.find(key);
    if (it == handler_data_.end())
      continue;

    mx_handle_t handle = it->second.handle;
    port_.cancel(handle, key);
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
  mx_time_t deadline = 0;
  if (next_run_time == ftl::TimePoint::Max()) {
    deadline = MX_TIME_INFINITE;
  } else if (next_run_time > now) {
    deadline = next_run_time.ToEpochDelta().ToNanoseconds();
  }

  // TODO(abarth): Use a priority queue to track the nearest deadlines.
  for (const auto& entry : handler_data_) {
    const HandlerData& handler_data = entry.second;
    if (handler_data.deadline <= now) {
      deadline = 0;
      break;
    } else if (handler_data.deadline != ftl::TimePoint::Max()) {
      mx_time_t handle_deadline =
          handler_data.deadline.ToEpochDelta().ToNanoseconds();
      deadline = std::min(deadline, handle_deadline);
    }
  }

  mx_port_packet_t packet;
  const mx_status_t wait_status = port_.wait(deadline, &packet, 0);

  // Update now after waiting.
  now = ftl::TimePoint::Now();

  // Handle errors which indicate bugs in "our" code (e.g. race conditions)
  if (wait_status != MX_OK && wait_status != MX_ERR_TIMED_OUT) {
    FTL_DCHECK(false) << "Unexpected wait status: " << wait_status;
    return now;
  }

  // Deliver timeouts.
  // FIXME(jeffbrown): If the port is always busy, we might never deliver
  // timeouts to handlers which have already passed their deadline.
  if (wait_status == MX_ERR_TIMED_OUT) {
    // TODO(abarth): Use a priority queue to track the nearest deadlines.
    std::vector<HandlerKey> keys;
    for (const auto& entry : handler_data_) {
      if (entry.second.deadline <= now)
        keys.push_back(entry.first);
    }
    CancelHandlers(std::move(keys), MX_ERR_TIMED_OUT);
    return now;
  }

  // Drain the incoming queue when new tasks are available.
  if (packet.key == kDrainKey) {
    ReloadQueue();
    return now;
  }

  // Deliver pending signals.
  FTL_CHECK(packet.type == MX_PKT_TYPE_SIGNAL_ONE)
      << "Received unexpected packet type: " << packet.type;
  FTL_DCHECK(packet.status == MX_OK);
  auto it = handler_data_.find(packet.key);
  if (it == handler_data_.end()) {
    // We can currently get packets for a key after we've canceled, but that's
    // something we should fix in the kernel.
    return now;
  }

  // We should only wake up when one of the signals we asked for was actually
  // observed.
  FTL_DCHECK(packet.signal.trigger & packet.signal.observed);

  const HandlerData& handler_data = it->second;
  FTL_DCHECK(packet.signal.trigger == handler_data.trigger);
  handler_data.handler->OnHandleReady(handler_data.handle,
                                      packet.signal.observed,
                                      packet.signal.count);

  // TODO(abarth): Slip the "repeating" and "once" cases in the MessageLoop
  // API.
  if (handler_data_.find(packet.key) != handler_data_.end()) {
    mx_object_wait_async(handler_data.handle, port_.get(), packet.key,
                         handler_data.trigger, MX_WAIT_ASYNC_ONCE);
  }

  CallAfterTaskCallback();
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
  mx_port_packet_t packet{.key = kDrainKey};
  FTL_CHECK(port_.queue(&packet, 0u) == MX_OK);
}

bool MessageLoop::RunsTasksOnCurrentThread() {
  return g_current == this;
}

ftl::TimePoint MessageLoop::RunReadyTasks(ftl::TimePoint now) {
  // Only consider tasks which are already in the queue.  We only reload
  // the queue in response to a "drain packet" to ensure that we continue
  // making forward progress handling port packet.  If we were to reload
  // the queue here instead then we might starve handlers when the incoming
  // tasks queue is very busy.
  FTL_DCHECK(!should_quit_);
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

}  // namespace mtl
