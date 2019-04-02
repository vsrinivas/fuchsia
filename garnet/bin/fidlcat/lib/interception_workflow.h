// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FIDLCAT_LIB_INTERCEPTION_WORKFLOW_H_
#define GARNET_BIN_FIDLCAT_LIB_INTERCEPTION_WORKFLOW_H_

#include <lib/fit/function.h>

#include <string>

#include "garnet/bin/fidlcat/lib/zx_channel_params.h"
#include "src/developer/debug/shared/buffered_fd.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/process_observer.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target_observer.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace fidlcat {

class InterceptionWorkflow;

namespace internal {

class InterceptingThreadObserver : public zxdb::ThreadObserver {
 public:
  explicit InterceptingThreadObserver(InterceptionWorkflow* workflow)
      : workflow_(workflow) {}
  virtual void OnThreadStopped(
      zxdb::Thread* thread, debug_ipc::NotifyException::Type type,
      const std::vector<fxl::WeakPtr<zxdb::Breakpoint>>& hit_breakpoints)
      override;
  virtual ~InterceptingThreadObserver() {}

 private:
  InterceptionWorkflow* workflow_;
};

class InterceptingProcessObserver : public zxdb::ProcessObserver {
 public:
  explicit InterceptingProcessObserver(InterceptionWorkflow* workflow)
      : dispatcher_(workflow) {}
  virtual void DidCreateThread(zxdb::Process* process,
                               zxdb::Thread* thread) override {
    thread->AddObserver(&dispatcher_);
  }

  virtual ~InterceptingProcessObserver() {}

 private:
  InterceptingThreadObserver dispatcher_;
};

class InterceptingTargetObserver : public zxdb::TargetObserver {
 public:
  explicit InterceptingTargetObserver(InterceptionWorkflow* workflow)
      : dispatcher_(workflow) {}

  virtual void DidCreateProcess(zxdb::Target* target, zxdb::Process* process,
                                bool autoattached_to_new_process) override {
    process->AddObserver(&dispatcher_);
  }

  virtual ~InterceptingTargetObserver() {}

 private:
  InterceptingProcessObserver dispatcher_;
};

}  // namespace internal

using SimpleErrorFunction = std::function<void(const zxdb::Err&)>;

// Controls the interactions with the debug agent.
//
// Most of the operations on this API are synchronous.  They expect a loop
// running in another thread to deal with the actions, and waits for the loop to
// complete the actions before returning from the method calls.  In fidlcat,
// Go() is called in a separate thread to start the loop.  The other operations
// - Initialize, Connect, Attach, etc - post tasks to that loop that are
// executed by the other thread.
class InterceptionWorkflow {
 public:
  friend class internal::InterceptingThreadObserver;
  friend class DataForZxWriteTest;

  InterceptionWorkflow();
  ~InterceptionWorkflow();

  // For testing, you can provide your own |session| and |loop|
  InterceptionWorkflow(zxdb::Session* session,
                       debug_ipc::PlatformMessageLoop* loop);

  // Some initialization steps:
  // - Set the paths for the zxdb client to look for symbols.
  // - Make sure that the data are routed from the client to the session
  void Initialize(const std::vector<std::string>& symbol_paths);

  // Connect the workflow to the host/port pair given.  |and_then| is posted to
  // the loop on completion.
  void Connect(const std::string& host, uint16_t port,
               SimpleErrorFunction and_then);

  // Attach the workflow to the given koid.  Must be connected.  |and_then| is
  // posted to the loop on completion.
  void Attach(uint64_t process_koid, SimpleErrorFunction and_then);

  // Sets breakpoints for the various methods we intercept (zx_channel_*, etc).
  // For each target for which we are attached, |and_then_each| is posted to the
  // loop on completion.
  void SetBreakpoints(SimpleErrorFunction and_then_each);

  // Sets the user-callback to be run when we intercept a zx_channel_write call.
  void SetZxChannelWriteCallback(ZxChannelWriteCallback&& callback) {
    zx_channel_write_callback_ = std::move(callback);
  }

  // Starts running the loop.  Returns when loop is (asynchronously) terminated.
  void Go();

  void Shutdown() {
    loop_->PostTask(FROM_HERE, [this]() { loop_->QuitNow(); });
  }

  zxdb::Session* session() const { return session_; }

  InterceptionWorkflow(const InterceptionWorkflow&) = delete;
  InterceptionWorkflow& operator=(const InterceptionWorkflow&) = delete;

 private:
  void OnZxChannelWrite(zxdb::Thread* thread);

  debug_ipc::BufferedFD buffer_;
  zxdb::Session* session_;
  bool delete_session_;
  debug_ipc::PlatformMessageLoop* loop_;
  bool delete_loop_;

  internal::InterceptingTargetObserver observer_;
  ZxChannelWriteCallback zx_channel_write_callback_;

  static const char kZxChannelWriteName[];
};

}  // namespace fidlcat

#endif  // GARNET_BIN_FIDLCAT_LIB_INTERCEPTION_WORKFLOW_H_
