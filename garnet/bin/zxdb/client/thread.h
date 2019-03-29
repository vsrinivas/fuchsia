// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/setting_store.h"
#include "garnet/bin/zxdb/client/stack.h"
#include "garnet/bin/zxdb/client/thread_observer.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/observer_list.h"
#include "src/developer/debug/ipc/protocol.h"

namespace zxdb {

class Err;
class Frame;
struct InputLocation;
class Process;
class RegisterSet;
class ThreadController;

// The flow control commands on this object (Pause, Continue, Step...) apply
// only to this thread (other threads will continue to run or not run
// as they were previously).
class Thread : public ClientObject {
 public:
  explicit Thread(Session* session);
  ~Thread() override;

  void AddObserver(ThreadObserver* observer);
  void RemoveObserver(ThreadObserver* observer);

  fxl::WeakPtr<Thread> GetWeakPtr();

  // Guaranteed non-null.
  virtual Process* GetProcess() const = 0;

  virtual uint64_t GetKoid() const = 0;
  virtual const std::string& GetName() const = 0;

  // The state of the thread isn't necessarily up-to-date. There are no
  // system messages for a thread transitioning to suspended, for example.
  // To make sure this is up-to-date, call Process::SyncThreads() or
  // Thread::SyncFrames().
  virtual debug_ipc::ThreadRecord::State GetState() const = 0;
  virtual debug_ipc::ThreadRecord::BlockedReason GetBlockedReason() const = 0;

  virtual void Pause() = 0;
  virtual void Continue() = 0;

  // Continues the thread using the given ThreadController. This is used
  // to implement the more complex forms of stepping.
  //
  // The on_continue callback does NOT indicate that the thread stopped again.
  // This is because many thread controllers may need to do asynchronous setup
  // that could fail. It is issued when the thread is actually resumed or when
  // the resumption fails.
  //
  // The on_continue callback may be issued reentrantly from within the stack
  // of the ContinueWith call if the controller was ready synchronously.
  //
  // On failure the ThreadController will be removed and the thread will not
  // be continued.
  virtual void ContinueWith(std::unique_ptr<ThreadController> controller,
                            std::function<void(const Err&)> on_continue) = 0;

  // Sets the thread's IP to the given location. This requires that the thread
  // be stopped. It will not resume the thread.
  //
  // Setting the location is asynchronous. At the time of the callback being
  // issued, the frames of the thread will be updated to the latest state.
  //
  // Resuming the thread after issuing but before the callback is executed will
  // pick up the new location (if any) because the requests will be ordered.
  // But because the jump request may fail, the caller isn't guaranteed what
  // location will be resumed from unless it waits for the callback.
  virtual void JumpTo(uint64_t new_address,
                      std::function<void(const Err&)> cb) = 0;

  // Notification from a ThreadController that it has completed its job. The
  // thread controller should be removed from this thread and deleted.
  virtual void NotifyControllerDone(ThreadController* controller) = 0;

  virtual void StepInstruction() = 0;

  // Returns the stack object associated with this thread.
  virtual const Stack& GetStack() const = 0;
  virtual Stack& GetStack() = 0;

  // Obtains the state of the registers for a particular thread.
  // The thread must be stopped in order to get the values.
  //
  // The returned structures are architecture independent, but the contents
  // will be dependent on the architecture the target is running on.
  virtual void ReadRegisters(
      std::vector<debug_ipc::RegisterCategory::Type> cats_to_get,
      std::function<void(const Err&, const RegisterSet&)>) = 0;

  // Provides the setting schema for this object.
  static fxl::RefPtr<SettingSchema> GetSchema();

  SettingStore& settings() { return settings_; }

 protected:
  fxl::ObserverList<ThreadObserver>& observers() { return observers_; }

  SettingStore settings_;

 private:
  fxl::ObserverList<ThreadObserver> observers_;
  fxl::WeakPtrFactory<Thread> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace zxdb
