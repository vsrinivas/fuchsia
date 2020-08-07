// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_H_

#include <stddef.h>
#include <stdint.h>

#include <string>

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

  // The state of the thread isn't necessarily up-to-date. There are no system messages for a thread
  // transitioning to suspended, for example. To make sure this is up-to-date, call
  // Process::SyncThreads() or Thread::SyncFrames().
  virtual debug_ipc::ThreadRecord::State GetState() const = 0;
  virtual debug_ipc::ThreadRecord::BlockedReason GetBlockedReason() const = 0;

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
  virtual void ContinueWith(std::unique_ptr<ThreadController> controller,
                            fit::callback<void(const Err&)> on_continue) = 0;

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

  virtual void StepInstruction() = 0;

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
