// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_H_

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "lib/fit/defer.h"
#include "lib/fit/function.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/client/map_setting_store.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/observer_list.h"

namespace zxdb {

class Err;
class Frame;
class Process;
class ThreadController;

// The flow control commands on this object (Pause, Continue, Step...) apply only to this thread
// (other threads will continue to run or not run as they were previously).
class Thread : public ClientObject {
 public:
  explicit Thread(Session* session);
  ~Thread() override;

  fxl::WeakPtr<Thread> GetWeakPtr();

  // Guaranteed non-null.
  virtual Process* GetProcess() const = 0;

  virtual uint64_t GetKoid() const = 0;
  virtual const std::string& GetName() const = 0;

  // Returns the current state of the thread.
  //
  // The state of the thread isn't necessarily up-to-date. In cases where we know the state isn't
  // up-to-date (we've asked it to do change but haven't heard back), GetState() will return a
  // nullopt. But the thread state could have changed out from under us or there could be a race
  // with the target, so a present state isn't guaranteed correct.
  //
  // To force an update, call Process::SyncThreads() or Thread::SyncFrames().
  virtual std::optional<debug_ipc::ThreadRecord::State> GetState() const = 0;
  virtual debug_ipc::ThreadRecord::BlockedReason GetBlockedReason() const = 0;

  // The "blocked on exception" state has a special query function since that's the only blocked
  // state that has valid frames.
  //
  // The other states that support valid frames (suspended and "core dump") are checked by
  // CurrentStopSupportsFrames(). Theoretically there should always be at least one frame in the
  // GetStack() if this returns true.
  bool IsBlockedOnException() const;
  bool CurrentStopSupportsFrames() const;

  // Pauses (suspends in Zircon terms) the thread, it does not affect other threads or processes.
  //
  // The backend will try to ensure the thread is actually paused before issuing the on_paused
  // callback. But this is best effort and not guaranteed: both because there's a timeout for the
  // synchronous suspending and because a different continue message could race with the reply.
  virtual void Pause(fit::callback<void()> on_paused) = 0;

  // Continues the thread, optionally forwarding the associated exception as
  // second-chance, allowing the process-level handler a chance to resolve the
  // exception before sending it back to the debugger; else, the exception is
  // marked as handled and the thread is resumed.
  virtual void Continue(bool forward_exception) = 0;

  // Continues the thread using the given ThreadController. This is used to implement the more
  // complex forms of stepping.
  //
  // The on_continue callback does NOT indicate that the thread stopped again. It indicates the
  // thread controller has completed setup properly (some may fail if they depend on some thread
  // state to start). When the step is complete an exception will be delivered via the thread
  // observer (it's not always possible to correlate a given thread suspension with a given step
  // operation).
  //
  // The on_continue callback may be issued reentrantly from within the stack of the ContinueWith
  // call if the controller was ready synchronously.
  //
  // On failure the ThreadController will be removed and the thread will not be continued.
  //
  // See also CancelAllThreadControllers() for aborting the controller.
  virtual void ContinueWith(std::unique_ptr<ThreadController> controller,
                            fit::callback<void(const Err&)> on_continue) = 0;

  // Enqueues a possibly-asynchronous task to execute after the current thread controllers have
  // completed handling a stop notification but before the thread is resumed or the stop
  // notification is passed to the user. If the thread is destroyed or manually resumed, any pending
  // tasks will be deleted without being run. This function must only be called during the thread
  // controller OnThreadStop() handlers.
  //
  // This is an injection point for asynchronous tasks to execute in the middle of stepping without
  // forcing the thread controllers to run asynchronously (which would complicate the code).
  //
  // All post-stop tasks enqueued by the thread controllers will be executed in the order they were
  // added. Completion of eash task is indicated by the execution of the callback argument which
  // allows the tasks to do asynchronous work. Executing the callback will either run the next task,
  // notify the user of the stop, or continue the program.
  //
  // The tasks are owned by the thread so the thread pointer is guarateed to be in-scope at the
  // time of the callback and it is safe to capture in the initial lambda. BUT the thread might get
  // deleted if the task does any asynchronous work so if the task enqueues any followup or
  // asynchronous work, it should take a WeakPtr to the thread.
  //
  // When the post-stop task is done, it should issue the task_completion callback. The
  // deferred_callback will automatically run when it goes out of scope, so normally the callback
  // would move it to keep it alive as long as the post-stop task is continuing, and then let it
  // automatically issue when the work returns.
  using PostStopTask = fit::callback<void(fit::deferred_callback task_completion_signaler)>;
  virtual void AddPostStopTask(PostStopTask task) = 0;

  // Stops all thread controllers which may be doing automatic stepping. The thread will be in the
  // state it was in last, which might be running if it was currently running, or it might be
  // stopped if a step operation was in place.
  virtual void CancelAllThreadControllers() = 0;

  // Used by ThreadControllers that need to perform asynchronous operations from a thread stop.
  // When OnThreadStop() returns kFuture, the thread controller is responsible for calling this
  // to re-evaluate the thread controller state. See thread_controller.h comments.
  //
  // The parameter allows optionally overriding the exception type for the re-delivery of the
  // stop notification. Often thread controllers will want to override this to "none" to force
  // a re-evaluation of the current location independent of the exception type. If the parameter is
  // nullopt, the original exception type will be used.
  virtual void ResumeFromAsyncThreadController(std::optional<debug_ipc::ExceptionType> type) = 0;

  // Sets the thread's IP to the given location. This requires that the thread be stopped. It will
  // not resume the thread.
  //
  // Setting the location is asynchronous. At the time of the callback being issued, the frames of
  // the thread will be updated to the latest state.
  //
  // Resuming the thread after issuing but before the callback is executed will pick up the new
  // location (if any) because the requests will be ordered. But because the jump request may fail,
  // the caller isn't guaranteed what location will be resumed from unless it waits for the
  // callback.
  virtual void JumpTo(uint64_t new_address, fit::callback<void(const Err&)> cb) = 0;

  // Notification from a ThreadController that it has completed its job. The thread controller
  // should be removed from this thread and deleted.
  virtual void NotifyControllerDone(ThreadController* controller) = 0;

  virtual void StepInstructions(uint64_t count) = 0;

  // Returns the stack object associated with this thread.
  virtual const Stack& GetStack() const = 0;
  virtual Stack& GetStack() = 0;

  // Provides the setting schema for this object.
  static fxl::RefPtr<SettingSchema> GetSchema();

  MapSettingStore& settings() { return settings_; }

 protected:
  MapSettingStore settings_;

 private:
  fxl::WeakPtrFactory<Thread> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_H_
