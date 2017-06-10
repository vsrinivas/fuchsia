// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/tasks/message_loop.h"

#include <utility>

#include "lib/ftl/logging.h"

namespace mtl {
namespace {

thread_local MessageLoop* g_current;

}  // namespace

class MessageLoop::TaskRecord : public async::Task {
 public:
  TaskRecord(mx_time_t deadline, ftl::Closure task);
  ~TaskRecord() override;

  async_task_result_t Handle(async_t* async, mx_status_t status) override;

 private:
  ftl::Closure task_;
};

class MessageLoop::HandlerRecord : public async::WaitWithTimeout {
 public:
  HandlerRecord(mx_handle_t object,
                mx_signals_t trigger,
                mx_time_t deadline,
                MessageLoop* loop,
                MessageLoopHandler* handler,
                HandlerKey key);
  ~HandlerRecord() override;

  async_wait_result_t Handle(async_t* async,
                             mx_status_t status,
                             const mx_packet_signal_t* signal) override;

 private:
  MessageLoop* loop_;
  MessageLoopHandler* handler_;
  HandlerKey key_;
};

MessageLoop::MessageLoop()
    : MessageLoop(ftl::MakeRefCounted<internal::IncomingTaskQueue>()) {}

MessageLoop::MessageLoop(
    ftl::RefPtr<internal::IncomingTaskQueue> incoming_tasks)
    : loop_config_{.make_default_for_current_thread = true,
                   .epilogue = &MessageLoop::Epilogue,
                   .data = this},
      loop_(&loop_config_),
      task_runner_(std::move(incoming_tasks)) {
  FTL_DCHECK(!g_current) << "At most one message loop per thread.";
  g_current = this;

  MessageLoop::incoming_tasks()->InitDelegate(this);
}

MessageLoop::~MessageLoop() {
  FTL_DCHECK(g_current == this)
      << "Message loops must be destroyed on their own threads.";

  incoming_tasks()->ClearDelegate();

  loop_.Shutdown();
  FTL_DCHECK(handlers_.empty());

  g_current = nullptr;
}

MessageLoop* MessageLoop::GetCurrent() {
  return g_current;
}

void MessageLoop::PostTask(ftl::Closure task, ftl::TimePoint target_time) {
  // TODO(jeffbrown): Consider allocating tasks from a pool.
  auto record = new TaskRecord(target_time.ToEpochDelta().ToNanoseconds(),
                               std::move(task));

  mx_status_t status = record->Post(loop_.async());
  if (status == MX_ERR_BAD_STATE) {
    // Suppress request when shutting down.
    delete record;
    return;
  }

  // The record will be destroyed when the task runs.
  FTL_CHECK(status == MX_OK) << "Failed to post task: status=" << status;
}

MessageLoop::HandlerKey MessageLoop::AddHandler(MessageLoopHandler* handler,
                                                mx_handle_t handle,
                                                mx_signals_t trigger,
                                                ftl::TimeDelta timeout) {
  FTL_DCHECK(g_current == this);
  FTL_DCHECK(handler);
  FTL_DCHECK(handle != MX_HANDLE_INVALID);

  // TODO(jeffbrown): Consider allocating handlers from a pool.
  HandlerKey key = next_handler_key_++;
  auto record = new HandlerRecord(handle, trigger, timeout.ToNanoseconds(),
                                  this, handler, key);
  mx_status_t status = record->Begin(loop_.async());
  if (status == MX_ERR_BAD_STATE) {
    // Suppress request when shutting down.
    delete record;
    return key;
  }

  // The record will be destroyed when the handler runs or is removed.
  FTL_CHECK(status == MX_OK) << "Failed to add handler: status=" << status;
  handlers_.emplace(key, record);
  return key;
}

void MessageLoop::RemoveHandler(HandlerKey key) {
  FTL_DCHECK(g_current == this);

  auto it = handlers_.find(key);
  if (it == handlers_.end())
    return;

  HandlerRecord* record = it->second;
  handlers_.erase(it);

  if (current_handler_ == record) {
    current_handler_removed_ = true;  // defer cleanup
  } else {
    mx_status_t status = record->Cancel(loop_.async());
    FTL_CHECK(status == MX_OK) << "Failed to cancel handler: status=" << status;
    delete record;
  }
}

bool MessageLoop::HasHandler(HandlerKey key) const {
  FTL_DCHECK(g_current == this);

  return handlers_.find(key) != handlers_.end();
}

void MessageLoop::Run() {
  FTL_DCHECK(g_current == this);

  FTL_CHECK(!is_running_) << "Cannot run a nested message loop.";
  is_running_ = true;

  mx_status_t status = loop_.Run();
  FTL_CHECK(status == MX_OK || status == MX_ERR_CANCELED)
      << "Loop stopped abnormally: status=" << status;
  loop_.ResetQuit();

  FTL_DCHECK(is_running_);
  is_running_ = false;
}

void MessageLoop::QuitNow() {
  FTL_DCHECK(g_current == this);

  if (is_running_)
    loop_.Quit();
}

void MessageLoop::PostQuitTask() {
  task_runner()->PostTask([this]() { QuitNow(); });
}

bool MessageLoop::RunsTasksOnCurrentThread() {
  return g_current == this;
}

void MessageLoop::SetAfterTaskCallback(ftl::Closure callback) {
  FTL_DCHECK(g_current == this);

  after_task_callback_ = std::move(callback);
}

void MessageLoop::ClearAfterTaskCallback() {
  FTL_DCHECK(g_current == this);

  after_task_callback_ = ftl::Closure();
}

void MessageLoop::Epilogue(async_t* async, void* data) {
  auto loop = static_cast<MessageLoop*>(data);
  // TODO(jeffbrown): The tests currently assert that the callbacks aren't
  // invoked when quitting but this seems asymmetrical.  Can we change this
  // behavior?
  if (loop->after_task_callback_ &&
      loop->loop_.GetState() == ASYNC_LOOP_RUNNABLE)
    loop->after_task_callback_();
}

MessageLoop::TaskRecord::TaskRecord(mx_time_t deadline, ftl::Closure task)
    : async::Task(deadline, ASYNC_HANDLE_SHUTDOWN), task_(std::move(task)) {}

MessageLoop::TaskRecord::~TaskRecord() {}

async_task_result_t MessageLoop::TaskRecord::Handle(async_t* async,
                                                    mx_status_t status) {
  if (status == MX_OK)
    task_();
  delete this;
  return ASYNC_TASK_FINISHED;
}

MessageLoop::HandlerRecord::HandlerRecord(mx_handle_t object,
                                          mx_signals_t trigger,
                                          mx_time_t deadline,
                                          MessageLoop* loop,
                                          MessageLoopHandler* handler,
                                          HandlerKey key)
    : async::WaitWithTimeout(object, trigger, deadline, ASYNC_HANDLE_SHUTDOWN),
      loop_(loop),
      handler_(handler),
      key_(key) {}

MessageLoop::HandlerRecord::~HandlerRecord() {}

async_wait_result_t MessageLoop::HandlerRecord::Handle(
    async_t* async,
    mx_status_t status,
    const mx_packet_signal_t* signal) {
  FTL_DCHECK(!loop_->current_handler_);
  loop_->current_handler_ = this;

  if (status == MX_OK) {
    handler_->OnHandleReady(object(), signal->observed, signal->count);
  } else {
    if (status == MX_ERR_CANCELED)
      status = MX_ERR_BAD_STATE;  // TODO(jeffbrown): remove this conversion
    handler_->OnHandleError(object(), status);

    if (!loop_->current_handler_removed_) {
      auto it = loop_->handlers_.find(key_);
      FTL_DCHECK(it != loop_->handlers_.end());
      loop_->handlers_.erase(it);
      loop_->current_handler_removed_ = true;
    }
  }

  FTL_DCHECK(loop_->current_handler_ == this);
  loop_->current_handler_ = nullptr;
  if (!loop_->current_handler_removed_)
    return ASYNC_WAIT_AGAIN;

  loop_->current_handler_removed_ = false;
  delete this;
  return ASYNC_WAIT_FINISHED;
}

}  // namespace mtl
