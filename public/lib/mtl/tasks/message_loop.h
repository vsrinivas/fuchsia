// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_TASKS_MESSAGE_LOOP_H_
#define LIB_MTL_TASKS_MESSAGE_LOOP_H_

#include <magenta/types.h>
#include <mx/event.h>

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

  // Constructs a message loop with an empty task queue. The message loop is
  // bound to the current thread.
  MessageLoop();

  // Constructs a message loop that will begin by draining the tasks already
  // present in the |incoming_tasks| queue. The message loop is bound to the
  // current thread.
  explicit MessageLoop(ftl::RefPtr<internal::IncomingTaskQueue> incoming_tasks);

  ~MessageLoop() override;

  // Returns the message loop associated with the current thread, if any.
  static MessageLoop* GetCurrent();

  // Return an interface for posting tasks to this message loop.
  const ftl::RefPtr<ftl::TaskRunner>& task_runner() const {
    return task_runner_;
  }

  // Adds a |handler| that the message loop calls when the |handle| triggers one
  // of the given |handle_signals| or when |timeout| elapses, whichever happens
  // first.
  //
  // The returned key can be used to remove the callback. The returned key will
  // always be non-zero.
  HandlerKey AddHandler(MessageLoopHandler* handler,
                        mx_handle_t handle,
                        mx_signals_t handle_signals,
                        ftl::TimeDelta timeout);

  // The message loop will no longer call the handler identified by the key. It
  // is an error to call this function with a key that doesn't correspond to a
  // currently registered callback.
  void RemoveHandler(HandlerKey key);

  // Returns whether the message loop has a handler registered with the given
  // key.
  bool HasHandler(HandlerKey key) const;

  // The message loop will call |callback| after each task that execute and
  // after each time it signals a handler. If the message loop already has an
  // after task callback set, this function will replace it with this one.
  void SetAfterTaskCallback(ftl::Closure callback);

  // The message loop will no longer call the registered after task callback, if
  // any.
  void ClearAfterTaskCallback();

  // Causes the message loop to run tasks until |QuitNow| is called. If no tasks
  // are available, the message loop with block and wait for tasks to be posted
  // via the |task_runner|.
  void Run();

  // Prevents further tasks from running and returns from |Run|. Must be called
  // while |Run| is on the stack.
  void QuitNow();

  // Posts a task to the queue that calls |QuitNow|. Useful for gracefully
  // ending the message loop. Can be called whether or not |Run| is on the
  // stack.
  void PostQuitTask();

 private:
  // Contains the data needed to track a request to AddHandler().
  struct HandlerData {
    MessageLoopHandler* handler = nullptr;
    mx_handle_t handle = MX_HANDLE_INVALID;
    mx_signals_t signals = MX_SIGNAL_NONE;
    ftl::TimePoint deadline;
  };

  // |internal::TaskQueueDelegate| implementation:
  void ScheduleDrainIncomingTasks() override;
  bool RunsTasksOnCurrentThread() override;

  void ReloadQueue();
  ftl::TimePoint RunReadyTasks(ftl::TimePoint now);
  ftl::TimePoint Wait(ftl::TimePoint now, ftl::TimePoint next_run_time);
  void RunTask(const internal::PendingTask& pending_task);
  void NotifyHandlers(ftl::TimePoint now, mx_status_t result);
  void CallAfterTaskCallback();

  internal::IncomingTaskQueue* incoming_tasks() {
    return static_cast<internal::IncomingTaskQueue*>(task_runner_.get());
  }

  ftl::RefPtr<ftl::TaskRunner> task_runner_;

  ftl::Closure after_task_callback_;

  bool should_quit_ = false;
  bool is_running_ = false;
  std::priority_queue<internal::PendingTask> queue_;
  mx::event event_;

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
    std::vector<mx_handle_t> handles;
    std::vector<mx_signals_t> signals;

    size_t size() const { return keys.size(); }

    void Set(size_t index,
             HandlerKey key,
             mx_handle_t handle,
             mx_signals_t signals);
    void Resize(size_t size);
  };

  WaitState wait_state_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MessageLoop);
};

}  // namespace mtl

#endif  // LIB_MTL_TASKS_MESSAGE_LOOP_H_
