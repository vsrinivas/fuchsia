// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_INTERCEPTION_WORKFLOW_H_
#define TOOLS_FIDLCAT_LIB_INTERCEPTION_WORKFLOW_H_

#include <lib/fit/function.h>

#include <string>

#include "src/developer/debug/shared/buffered_fd.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/process_observer.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target_observer.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "src/developer/debug/zxdb/common/err.h"

// TODO: Look into this.  Removing the hack that led to this (in
// debug_ipc/helper/message_loop.h) seems to work, except it breaks SDK builds
// on CQ in a way I can't repro locally.
#undef __TA_REQUIRES

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

class InterceptionWorkflow;
class SyscallDecoderDispatcher;
class SyscallDecoder;

class InterceptingThreadObserver : public zxdb::ThreadObserver {
 public:
  explicit InterceptingThreadObserver(InterceptionWorkflow* workflow) : workflow_(workflow) {}

  InterceptingThreadObserver(const InterceptingThreadObserver&) = delete;
  InterceptingThreadObserver& operator=(const InterceptingThreadObserver&) = delete;

  virtual void OnThreadStopped(
      zxdb::Thread* thread, debug_ipc::NotifyException::Type type,
      const std::vector<fxl::WeakPtr<zxdb::Breakpoint>>& hit_breakpoints) override;

  virtual ~InterceptingThreadObserver() {}

  void Register(int64_t koid, SyscallDecoder* decoder);

  void CreateNewBreakpoint(zxdb::BreakpointSettings& settings);

 private:
  InterceptionWorkflow* workflow_;
  std::map<int64_t, SyscallDecoder*> breakpoint_map_;
};

class InterceptingProcessObserver : public zxdb::ProcessObserver {
 public:
  explicit InterceptingProcessObserver(InterceptionWorkflow* workflow) : dispatcher_(workflow) {}

  InterceptingProcessObserver(const InterceptingProcessObserver&) = delete;
  InterceptingProcessObserver& operator=(const InterceptingProcessObserver&) = delete;

  virtual void DidCreateThread(zxdb::Process* process, zxdb::Thread* thread) override {
    thread->AddObserver(&dispatcher_);
  }

  virtual ~InterceptingProcessObserver() {}

  InterceptingThreadObserver& thread_observer() { return dispatcher_; }

 private:
  InterceptingThreadObserver dispatcher_;
};

class InterceptingTargetObserver : public zxdb::TargetObserver {
 public:
  explicit InterceptingTargetObserver(InterceptionWorkflow* workflow)
      : dispatcher_(workflow), workflow_(workflow) {}

  InterceptingTargetObserver(const InterceptingTargetObserver&) = delete;
  InterceptingTargetObserver& operator=(const InterceptingTargetObserver&) = delete;

  virtual void DidCreateProcess(zxdb::Target* target, zxdb::Process* process,
                                bool autoattached_to_new_process) override;

  virtual void WillDestroyProcess(zxdb::Target* target, zxdb::Process* process,
                                  DestroyReason reason, int exit_code) override;

  virtual ~InterceptingTargetObserver() {}

  InterceptingProcessObserver& process_observer() { return dispatcher_; }

 private:
  InterceptingProcessObserver dispatcher_;
  InterceptionWorkflow* workflow_;
};

using SimpleErrorFunction = std::function<void(const zxdb::Err&)>;
using KoidFunction = std::function<void(const zxdb::Err&, uint64_t)>;

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
  friend class InterceptingThreadObserver;
  friend class DataForZxChannelTest;
  friend class ProcessController;

  InterceptionWorkflow();
  ~InterceptionWorkflow();

  // For testing, you can provide your own |session| and |loop|
  InterceptionWorkflow(zxdb::Session* session, debug_ipc::PlatformMessageLoop* loop);

  // Some initialization steps:
  // - Set the paths for the zxdb client to look for symbols.
  // - Make sure that the data are routed from the client to the session
  void Initialize(const std::vector<std::string>& symbol_paths,
                  std::unique_ptr<SyscallDecoderDispatcher> syscall_decoder_dispatcher);

  // Connect the workflow to the host/port pair given.  |and_then| is posted to
  // the loop on completion.
  void Connect(const std::string& host, uint16_t port, SimpleErrorFunction and_then);

  // Attach the workflow to the given koids.  Must be connected.  |and_then| is
  // posted to the loop on completion.
  void Attach(const std::vector<uint64_t>& process_koids, KoidFunction and_then);

  // Detach from one target.  session() keeps track of details about the Target
  // object; this just reduces the number of targets to which we are attached by
  // one, and shuts down if we hit 0.
  void Detach();

  // Run the given |command| and attach to it.  Must be connected.  |and_then|
  // is posted to the loop on completion.
  void Launch(const std::vector<std::string>& command, KoidFunction and_then);

  // Run when a process matching the given |filter| regexp is started.  Must be
  // connected.  |and_then| is posted to the loop on completion.
  void Filter(const std::vector<std::string>& filter, KoidFunction and_then);

  // Sets breakpoints for the various methods we intercept (zx_channel_*, etc)
  // for the given |target|
  void SetBreakpoints(zxdb::Target* target = nullptr);

  // Sets breakpoints for the various methods we intercept (zx_channel_*, etc)
  // for the process with given |process_koid|.
  void SetBreakpoints(uint64_t process_koid);

  // Starts running the loop.  Returns when loop is (asynchronously) terminated.
  void Go();

  void Shutdown() {
    session()->Disconnect([this](const zxdb::Err& err) {
      loop_->PostTask(FROM_HERE, [this]() { loop_->QuitNow(); });
    });
  }

  zxdb::Session* session() const { return session_; }
  SyscallDecoderDispatcher* syscall_decoder_dispatcher() const {
    return syscall_decoder_dispatcher_.get();
  }

  InterceptionWorkflow(const InterceptionWorkflow&) = delete;
  InterceptionWorkflow& operator=(const InterceptionWorkflow&) = delete;

 private:
  zxdb::Target* GetTarget(uint64_t process_koid = ULLONG_MAX);

  void AddObserver(zxdb::Target* target);

  debug_ipc::BufferedFD buffer_;
  zxdb::Session* session_;
  std::vector<zxdb::Filter*> filters_;
  bool delete_session_;
  debug_ipc::PlatformMessageLoop* loop_;
  bool delete_loop_;
  // -1 means "we already shut down".
  int target_count_;

  std::unique_ptr<SyscallDecoderDispatcher> syscall_decoder_dispatcher_;

  InterceptingTargetObserver observer_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_INTERCEPTION_WORKFLOW_H_
