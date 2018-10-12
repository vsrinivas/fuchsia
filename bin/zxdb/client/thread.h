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
#include "garnet/bin/zxdb/client/thread_observer.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/observer_list.h"

namespace zxdb {

class Err;
class Frame;
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
  // To make sure this is up-to-date, call Process::SyncThreads().
  virtual debug_ipc::ThreadRecord::State GetState() const = 0;

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

  // Notification from a ThreadController that it has completed its job. The
  // thread controller should be removed from this thread and deleted.
  virtual void NotifyControllerDone(ThreadController* controller) = 0;

  virtual void StepInstruction() = 0;

  // Access to the stack frames for this thread at its current stopped
  // position. If a thread is running, the stack frames are not available.
  //
  // When a thread is stopped, it will have its 0th frame available (the
  // current IP and stack position) and the 1st (the calling frame) if
  // possible. So stopped threads will always have at least one result in the
  // vector returned by GetFrames(), and normally two.
  //
  // If the full backtrace is needed, SyncFrames() can be called which will
  // compute the full backtrace and issue the callback when complete. This
  // backtrace will be cached until the thread is resumed. HasAllFrames()
  // will return true if the full backtrace is currently available (= true) or
  // if only the current position is available (= false).
  //
  // Since the running/stopped state of a thread isn't available synchronously
  // in a non-racy manner, you can always request a Sync of the frames if the
  // frames are not all available. If the thread is destroyed before the
  // backtrace can be issued, the callback will not be executed.
  //
  // If the thread is running when the request is processed, the callback will
  // be issued but a subsequent call to GetFrames() will return an empty vector
  // and HasAllFrames() will return false. This call can race with other
  // requests to resume a thread, so you can't make any assumptions about the
  // availability of the stack from the callback.
  //
  // The pointers in the vector returned by GetFrames() can be cached if the
  // code listens for ThreadObserver::OnThreadFramesInvalidated() and clears
  // the cache at that point.
  virtual std::vector<Frame*> GetFrames() const = 0;
  virtual bool HasAllFrames() const = 0;
  virtual void SyncFrames(std::function<void()> callback) = 0;

  // Computes the stack frame fingerprint for the stack frame at the given
  // index. This function requires that that the previous stack frame
  // (frame_index + 1) by present since the stack base is the SP of the
  // calling function.
  //
  // This function can always return the fingerprint for frame 0. Other
  // frames requires HasAllFrames() == true or it will assert.
  //
  // See frame.h for a discussion on stack frames.
  virtual FrameFingerprint GetFrameFingerprint(size_t frame_index) const = 0;

  // Obtains the state of the registers for a particular thread.
  // The thread must be stopped in order to get the values.
  //
  // The returned structures are architecture independent, but the contents
  // will be dependent on the architecture the target is running on.
  virtual void GetRegisters(
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
