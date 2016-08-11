// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_TASKS_MESSAGE_LOOP_H_
#define LIB_MTL_TASKS_MESSAGE_LOOP_H_

#include <mojo/system/wait.h>

#include <map>
#include <memory>
#include <queue>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/synchronization/waitable_event.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/mtl/handles/unique_handle.h"
#include "lib/mtl/tasks/incoming_task_queue.h"
#include "lib/mtl/tasks/message_loop_handler.h"
#include "lib/mtl/tasks/pending_task.h"

namespace mtl {

class MessageLoop : public internal::TaskQueueDelegate {
 public:
  using HandlerKey = uint64_t;

  MessageLoop();
  explicit MessageLoop(ftl::RefPtr<internal::IncomingTaskQueue> incoming_tasks);
  ~MessageLoop() override;

  static MessageLoop* GetCurrent();

  ftl::TaskRunner* task_runner() const { return incoming_tasks_.get(); }

  HandlerKey AddHandler(MessageLoopHandler* handler,
                        MojoHandle handle,
                        MojoHandleSignals handle_signals,
                        ftl::TimeDelta timeout);
  void RemoveHandler(HandlerKey key);
  bool HasHandler(HandlerKey key) const;

  void Run();
  void QuitNow();

  void PostQuitTask();

 private:
  // Contains the data needed to track a request to AddHandler().
  struct HandlerData {
    MessageLoopHandler* handler = nullptr;
    MojoHandle handle = MOJO_HANDLE_INVALID;
    MojoHandleSignals signals = MOJO_HANDLE_SIGNAL_NONE;
    ftl::TimePoint deadline;
  };

  // |internal::TaskQueueDelegate| implementation:
  void ScheduleDrainIncomingTasks() override;

  void ReloadQueue();
  ftl::TimePoint RunReadyTasks(ftl::TimePoint now);
  ftl::TimePoint Wait(ftl::TimePoint now, ftl::TimePoint next_run_time);
  void RunTask(const internal::PendingTask& pending_task);
  void NotifyHandlers(ftl::TimePoint now, MojoResult result);

  ftl::RefPtr<internal::IncomingTaskQueue> incoming_tasks_;

  bool should_quit_ = false;
  bool is_running_ = false;
  std::priority_queue<internal::PendingTask> queue_;
  UniqueHandle event_;

  // An ever increasing value assigned to each HandlerData::id. Used to detect
  // uniqueness while notifying. That is, while notifying expired timers we
  // copy |handler_data_| and only notify handlers whose id match. If the id
  // does not match it means the handler was removed then added so that we
  // shouldn't notify it.
  HandlerKey next_handler_key_ = 1u;

  using HandleToHandlerData = std::map<HandlerKey, HandlerData>;
  HandleToHandlerData handler_data_;

  class WaitState {
   public:
    std::vector<HandlerKey> keys;
    std::vector<MojoHandle> handles;
    std::vector<MojoHandleSignals> signals;

    size_t size() const { return keys.size(); }

    void Set(size_t index,
             HandlerKey key,
             MojoHandle handle,
             MojoHandleSignals signals);
    void Resize(size_t size);
  };

  WaitState wait_state_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MessageLoop);
};

}  // namespace mtl

#endif  // LIB_MTL_TASKS_MESSAGE_LOOP_H_
