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
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "tools/fidlcat/command_line_options.h"
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

  virtual void OnThreadStopped(zxdb::Thread* thread, const zxdb::StopInfo& info) override;

  virtual ~InterceptingThreadObserver() {}

  void Register(int64_t koid, SyscallDecoder* decoder);
  void AddExitBreakpoint(zxdb::Thread* thread, const fidlcat::Syscall& syscall, uint64_t address);

  void CreateNewBreakpoint(zxdb::Thread* thread, zxdb::BreakpointSettings& settings);

  uint64_t GetTimestamp();

 private:
  InterceptionWorkflow* workflow_;
  std::unordered_set<uint64_t> exit_breakpoints_;
  std::map<int64_t, SyscallDecoder*> breakpoint_map_;
  std::unordered_set<int64_t> threads_in_error_;
  // By default, the breakpoints at the end of a syscall are put permanently.
  // To test zxdb one shot breakpoints, you can change this value to true.
  bool one_shot_breakpoints_ = false;
};

class InterceptingProcessObserver : public zxdb::ProcessObserver {
 public:
  explicit InterceptingProcessObserver(InterceptionWorkflow* workflow) : workflow_(workflow) {}

  void DidCreateProcess(zxdb::Process* process, bool autoattached, uint64_t timestamp) override;
  void WillDestroyProcess(zxdb::Process* process, DestroyReason reason, int exit_code,
                          uint64_t timestamp) override;
  void OnSymbolLoadFailure(zxdb::Process* process, const zxdb::Err& err) override;

 private:
  InterceptionWorkflow* workflow_;
};

using SimpleErrorFunction = std::function<void(const zxdb::Err&)>;
using KoidFunction = std::function<void(const zxdb::Err&, zx_koid_t)>;

struct ProcessFilter {
  zxdb::Filter* filter;
  bool main_filter;
};

struct ConfiguredProcess {
  fxl::WeakPtr<zxdb::Process> process;
  // True if the process is a main process (monitored with --remote-name) and false if the process
  // is secondary (monitored with --extra-name).
  bool main_process;

  ConfiguredProcess(fxl::WeakPtr<zxdb::Process> process, bool main_process)
      : process(process), main_process(main_process) {}
};

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
  InterceptionWorkflow(zxdb::Session* session, debug::MessageLoop* loop);

  // Some initialization steps:
  // - Set the paths for the zxdb client to look for symbols.
  // - Make sure that the data are routed from the client to the session
  void Initialize(const std::vector<std::string>& symbol_index_files,
                  const std::vector<std::string>& symbol_paths,
                  const std::vector<std::string>& build_id_dirs,
                  const std::vector<std::string>& ids_txts,
                  const std::optional<std::string>& symbol_cache,
                  const std::vector<std::string>& symbol_servers,
                  std::unique_ptr<SyscallDecoderDispatcher> syscall_decoder_dispatcher,
                  bool quit_agent_on_exit);

  void AuthenticateServer(zxdb::SymbolServer* server);

  // Connect the workflow to the host/port pair given.  |and_then| is posted to
  // the loop on completion.
  void Connect(const std::string& host, uint16_t port, const SimpleErrorFunction& and_then);

  // Connect the workflow to the unix socket given.  |and_then| is posted to
  // the loop on completion.
  void UnixConnect(const std::string& unix_socket, const SimpleErrorFunction& and_then);

  // Attach the workflow to the given koids.
  void Attach(const std::vector<zx_koid_t>& process_koids);
  // Using the process tree record, attach the workflow to all processes which belong to a job
  // which matches remote_job_id or remote_job_name.
  void AttachToJobs(const debug_ipc::ProcessTreeRecord& record,
                    const std::vector<std::uint64_t>& remote_job_id,
                    const std::vector<std::string>& remote_job_name,
                    const std::vector<std::string>& remote_name,
                    const std::vector<std::string>& extra_name);

  // Called when a monitored process is detached/dead. This function can
  // called several times with the same koid.
  void ProcessDetached(zx_koid_t koid, uint64_t timestamp);

  // Detach from one target.  session() keeps track of details about the Target
  // object; this just reduces the number of targets to which we are attached by
  // one, and shuts down if we hit 0.
  void Detach();

  // Run the given |command| and attach to it.  Must be connected.  |and_then|
  // is posted to the loop on completion.
  void Launch(zxdb::Target* target, const std::vector<std::string>& command);

  // Run when a process matching the given |filter| regexp is started.  Must be
  // connected.  |and_then| is posted to the loop on completion.
  void Filter(const std::vector<std::string>& filter, bool main_filter, zxdb::Job* job);

  // Sets breakpoints for the various methods we intercept (zx_channel_*, etc)
  // for the given |process|. If the process is secondary and no main process is already monitored,
  // postpone the breakpoints' setting.
  void SetBreakpoints(zxdb::Process* process, uint64_t timestamp);

  // Actually set the breakpoints.
  void DoSetBreakpoints(zxdb::Process* process, uint64_t timestamp);

  // Starts running the loop.  Returns when loop is (asynchronously) terminated.
  static void Go();

  void Shutdown() {
    session()->Disconnect([this](const zxdb::Err& err) {
      loop_->PostTask(FROM_HERE, [this]() { loop_->QuitNow(); });
    });
  }

  zxdb::Target* GetTarget(zx_koid_t process_koid);
  zxdb::Target* GetNewTarget();

  bool HasSymbolServers() const;
  std::vector<zxdb::SymbolServer*> GetSymbolServers() const;

  zxdb::Session* session() const { return session_; }
  bool decode_events() const { return decode_events_; }
  std::map<zx_koid_t, ConfiguredProcess>& configured_processes() { return configured_processes_; }
  SyscallDecoderDispatcher* syscall_decoder_dispatcher() const {
    return syscall_decoder_dispatcher_.get();
  }

  std::unique_ptr<SyscallDecoderDispatcher> GetBackDispatcher() {
    return std::move(syscall_decoder_dispatcher_);
  }

  InterceptingThreadObserver& thread_observer() { return thread_observer_; }

  InterceptionWorkflow(const InterceptionWorkflow&) = delete;
  InterceptionWorkflow& operator=(const InterceptionWorkflow&) = delete;

 private:
  debug::BufferedFD buffer_;
  zxdb::Session* session_;
  std::vector<ProcessFilter> filters_;
  bool delete_session_;
  debug::MessageLoop* loop_;
  bool delete_loop_;
  bool decode_events_ = true;
  bool shutdown_done_ = false;
  bool server_authentication_error_ = false;
  int remaining_authentications_ = 0;

  // All the processes for which the breapoints have been set.
  std::map<zx_koid_t, ConfiguredProcess> configured_processes_;

  std::unique_ptr<SyscallDecoderDispatcher> syscall_decoder_dispatcher_;

  InterceptingProcessObserver process_observer_;
  InterceptingThreadObserver thread_observer_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_INTERCEPTION_WORKFLOW_H_
