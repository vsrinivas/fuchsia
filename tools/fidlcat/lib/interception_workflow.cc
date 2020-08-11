// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/interception_workflow.h"

#include <cstring>
#include <string>
#include <thread>

#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "tools/fidlcat/lib/decode_options.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

void InterceptingThreadObserver::OnThreadStopped(zxdb::Thread* thread, const zxdb::StopInfo& info) {
  FX_CHECK(thread) << "Internal error: Stopped in a breakpoint without a thread?";

  if (info.exception_type != debug_ipc::ExceptionType::kSoftwareBreakpoint) {
    FX_CHECK(info.hit_breakpoints.empty());
    if (threads_in_error_.find(thread->GetKoid()) == threads_in_error_.end()) {
      threads_in_error_.emplace(thread->GetKoid());
      workflow_->syscall_decoder_dispatcher()->DecodeException(workflow_, thread);
    }
    return;
  }

  if (info.hit_breakpoints.empty()) {
    // This can happen when we are shutting down fidlcat.
    // There is nothing to do => we just return.
    return;
  }

  // TODO(bug 47497) Uncomment this and fix the test bugs that create more than one breakpoint
  // at the same address.
  // FX_CHECK(info.hit_breakpoints.size() == 1)
  //     << "Internal error: more than one simultaneous breakpoint for thread " <<
  //     thread->GetKoid();

  // There a two possible breakpoints we can hit:
  //  - A breakpoint right before a system call (zx_channel_read,
  //    zx_channel_write, etc)
  //  - A breakpoint that we hit because we ran the system call to see what the
  //    result will be.

  // This is the breakpoint that we hit after running the system call.  The
  // initial breakpoint - the one on the system call - registered a callback in
  // this per-thread map, so that the next breakpoint on this thread would be
  // handled here.
  auto entry = breakpoint_map_.find(thread->GetKoid());
  if (entry != breakpoint_map_.end()) {
    entry->second->LoadSyscallReturnValue();
    // Erasing under the assumption that the next step will put it back, if
    // necessary.
    breakpoint_map_.erase(thread->GetKoid());
    return;
  }

  // If there was no registered breakpoint on this thread, we hit it because we
  // encountered a system call.  Run the callbacks associated with this system
  // call.
  for (auto& bp_ptr : info.hit_breakpoints) {
    zxdb::BreakpointSettings settings = bp_ptr->GetSettings();
    if (settings.locations.size() == 1u &&
        settings.locations[0].type == zxdb::InputLocation::Type::kName &&
        settings.locations[0].name.components().size() == 1u) {
      threads_in_error_.erase(thread->GetKoid());
      // Compare against the syscall->name() which is the syscall name not including the $plt
      // prefix. The Identifier component's name won't include this annotation without running
      // GetFullName() which is slower. We already checked that it's a $plt annotation above.
      auto syscall = workflow_->syscall_decoder_dispatcher()->SearchSyscall(
          settings.locations[0].name.components()[0].name());
      if (syscall == nullptr) {
        FX_LOGS(ERROR) << thread->GetProcess()->GetName() << ' ' << thread->GetProcess()->GetKoid()
                       << ':' << thread->GetKoid() << ": Internal error: breakpoint "
                       << settings.locations[0].name.components()[0].name() << " not managed";
        thread->Continue(false);
        return;
      }
      workflow_->syscall_decoder_dispatcher()->DecodeSyscall(this, thread, syscall);
      return;
    }
  }
  thread->Continue(false);
}

void InterceptingThreadObserver::Register(int64_t koid, SyscallDecoder* decoder) {
  breakpoint_map_[koid] = decoder;
}

void InterceptingThreadObserver::AddExitBreakpoint(zxdb::Thread* thread,
                                                   const std::string& syscall_name,
                                                   uint64_t address) {
  zxdb::BreakpointSettings settings;
  if (one_shot_breakpoints_) {
    settings.enabled = true;
    settings.name = syscall_name + "-return";
    settings.stop_mode = zxdb::BreakpointSettings::StopMode::kThread;
    settings.type = debug_ipc::BreakpointType::kSoftware;
    settings.locations.emplace_back(address);
    settings.scope = zxdb::ExecutionScope(thread);
    settings.one_shot = true;
  } else {
    if (exit_breakpoints_.find(address) != exit_breakpoints_.end()) {
      return;
    }

    exit_breakpoints_.emplace(address);

    settings.enabled = true;
    settings.name = syscall_name + "-return";
    settings.stop_mode = zxdb::BreakpointSettings::StopMode::kThread;
    settings.type = debug_ipc::BreakpointType::kSoftware;
    settings.locations.emplace_back(address);
    settings.scope = zxdb::ExecutionScope(thread->GetProcess()->GetTarget());
  }

  FX_VLOGS(2) << "Thread " << thread->GetKoid() << ": creating return value breakpoint for "
              << syscall_name << " at address " << std::hex << address << std::dec;
  CreateNewBreakpoint(thread, settings);
}

void InterceptingThreadObserver::CreateNewBreakpoint(zxdb::Thread* thread,
                                                     zxdb::BreakpointSettings& settings) {
  zxdb::Breakpoint* breakpoint = workflow_->session_->system().CreateNewBreakpoint();
  breakpoint->SetSettings(settings);
}

void InterceptingProcessObserver::DidCreateProcess(zxdb::Process* process, bool autoattached) {
  workflow_->syscall_decoder_dispatcher()->AddLaunchedProcess(process->GetKoid());
  workflow_->SetBreakpoints(process);
}

void InterceptingProcessObserver::WillDestroyProcess(zxdb::Process* process,
                                                     ProcessObserver::DestroyReason reason,
                                                     int exit_code) {
  workflow_->ProcessDetached(process->GetKoid());
}

InterceptionWorkflow::InterceptionWorkflow()
    : session_(new zxdb::Session()),
      delete_session_(true),
      loop_(new debug_ipc::PlatformMessageLoop()),
      delete_loop_(true),
      process_observer_(this),
      thread_observer_(this) {
  session_->process_observers().AddObserver(&process_observer_);
  session_->thread_observers().AddObserver(&thread_observer_);
}

InterceptionWorkflow::InterceptionWorkflow(zxdb::Session* session, debug_ipc::MessageLoop* loop)
    : session_(session),
      delete_session_(false),
      loop_(loop),
      delete_loop_(false),
      process_observer_(this),
      thread_observer_(this) {
  session_->process_observers().AddObserver(&process_observer_);
  session_->thread_observers().AddObserver(&thread_observer_);
}

InterceptionWorkflow::~InterceptionWorkflow() {
  session_->thread_observers().RemoveObserver(&thread_observer_);
  session_->process_observers().RemoveObserver(&process_observer_);
  if (delete_session_) {
    delete session_;
  }
  if (delete_loop_) {
    delete loop_;
  }
}

void InterceptionWorkflow::Initialize(
    const std::vector<std::string>& symbol_index_files,
    const std::vector<std::string>& symbol_paths, const std::vector<std::string>& build_id_dirs,
    const std::vector<std::string>& ids_txts, const std::optional<std::string>& symbol_cache,
    const std::vector<std::string>& symbol_servers,
    std::unique_ptr<SyscallDecoderDispatcher> syscall_decoder_dispatcher, bool quit_agent_on_exit) {
  syscall_decoder_dispatcher_ = std::move(syscall_decoder_dispatcher);

  if (quit_agent_on_exit) {
    session_->system().settings().SetBool(zxdb::ClientSettings::System::kQuitAgentOnExit, true);
  }

  // 1) Set up symbol index.

  // Stolen from zxdb/console/console_main.cc
  auto& system_settings = session_->system().settings();
  const char* home = std::getenv("HOME");

  if (symbol_cache) {
    // Legacy usage assumes a .build-id subdirectory will be created.
    system_settings.SetString(zxdb::ClientSettings::System::kSymbolCache,
                              *symbol_cache + "/.build-id");
  } else {
    // Default value for symbol_cache.
    if (home) {
      system_settings.SetString(zxdb::ClientSettings::System::kSymbolCache,
                                std::string(home) + "/.fuchsia/debug/symbol-cache");
    }
  }

  if (!symbol_index_files.empty()) {
    system_settings.SetList(zxdb::ClientSettings::System::kSymbolIndexFiles, symbol_index_files);
  } else {
    // Default value for symbol_index_files.
    if (home) {
      system_settings.SetList(zxdb::ClientSettings::System::kSymbolIndexFiles,
                              {std::string(home) + "/.fuchsia/debug/symbol-index"});
    }
  }

  if (!symbol_servers.empty()) {
    system_settings.SetList(zxdb::ClientSettings::System::kSymbolServers, symbol_servers);
  }

  if (!symbol_paths.empty()) {
    system_settings.SetList(zxdb::ClientSettings::System::kSymbolPaths, symbol_paths);
  }

  if (!build_id_dirs.empty()) {
    system_settings.SetList(zxdb::ClientSettings::System::kBuildIdDirs, build_id_dirs);
  }

  if (!ids_txts.empty()) {
    system_settings.SetList(zxdb::ClientSettings::System::kIdsTxts, ids_txts);
  }

  // 2) Ensure that the session correctly reads data off of the loop.
  buffer_.set_data_available_callback([this]() { session_->OnStreamReadable(); });

  // 3) Provide a loop, if none exists.
  if (debug_ipc::MessageLoop::Current() == nullptr) {
    std::string error_message;
    bool success = loop_->Init(&error_message);
    FX_CHECK(success) << error_message;
  }

  // 4) Initialize the symbol servers.
  if (!symbol_servers.empty()) {
    session_->system().settings().SetList(zxdb::ClientSettings::System::kSymbolServers,
                                          symbol_servers);
  }
}

void InterceptionWorkflow::Connect(const std::string& host, uint16_t port,
                                   const SimpleErrorFunction& and_then) {
  zxdb::SessionConnectionInfo connect_info = {zxdb::SessionConnectionType::kNetwork, host, port};
  session_->Connect(connect_info, [and_then](const zxdb::Err& err) { and_then(err); });
}

// Helper function that finds a target for fidlcat to attach itself to. The
// target with |process_koid| must already be running.
zxdb::Target* InterceptionWorkflow::GetTarget(zx_koid_t process_koid) {
  for (zxdb::Target* target : session_->system().GetTargets()) {
    if (target->GetProcess() && target->GetProcess()->GetKoid() == process_koid) {
      return target;
    }
  }
  return session_->system().CreateNewTarget(nullptr);
}

zxdb::Target* InterceptionWorkflow::GetNewTarget() {
  for (zxdb::Target* target : session_->system().GetTargets()) {
    if (target->GetState() == zxdb::Target::State::kNone) {
      return target;
    }
  }
  return session_->system().CreateNewTarget(nullptr);
}

bool InterceptionWorkflow::HasSymbolServers() const {
  return !session_->system().GetSymbolServers().empty();
}

std::vector<zxdb::SymbolServer*> InterceptionWorkflow::GetSymbolServers() const {
  return session_->system().GetSymbolServers();
}

void InterceptionWorkflow::Attach(const std::vector<zx_koid_t>& process_koids) {
  for (zx_koid_t process_koid : process_koids) {
    // Get a target for this process.
    zxdb::Target* target = GetTarget(process_koid);
    // If we are already attached, then we are done.
    if (target->GetProcess()) {
      FX_CHECK(target->GetProcess()->GetKoid() == process_koid)
          << "Internal error: target attached to wrong process";
      continue;
    }

    // The debugger is not yet attached to the process.  Attach to it.
    target->Attach(process_koid, [this, target, process_koid](fxl::WeakPtr<zxdb::Target> /*target*/,
                                                              const zxdb::Err& err) {
      if (!err.ok()) {
        int64_t timestamp = time(nullptr);
        Process* process = syscall_decoder_dispatcher()->SearchProcess(process_koid);
        if (process == nullptr) {
          process = syscall_decoder_dispatcher()->CreateProcess("", process_koid, nullptr);
        }
        syscall_decoder_dispatcher()->AddProcessMonitoredEvent(
            std::make_shared<ProcessMonitoredEvent>(timestamp, process, err.msg()));
        return;
      }

      SetBreakpoints(target->GetProcess());
    });
  }
}

void InterceptionWorkflow::ProcessDetached(zx_koid_t koid) {
  if (configured_processes_.find(koid) == configured_processes_.end()) {
    return;
  }
  configured_processes_.erase(koid);
  int64_t timestamp = time(nullptr);
  Process* process = syscall_decoder_dispatcher()->SearchProcess(koid);
  if (process == nullptr) {
    FX_LOGS(ERROR) << "Can't find process with koid=" << koid;
  } else {
    syscall_decoder_dispatcher()->AddStopMonitoringEvent(
        std::make_shared<StopMonitoringEvent>(timestamp, process));
  }
  Detach();
}

void InterceptionWorkflow::Detach() {
  for (const auto& configured_process : configured_processes_) {
    if (configured_process.second.main_process) {
      // One main process is still running => don't shutdown fidlcat.
      return;
    }
  }
  if (!shutdown_done_) {
    shutdown_done_ = true;
    Shutdown();
  }
}

void InterceptionWorkflow::Filter(const std::vector<std::string>& filter, bool main_filter) {
  if (filter.empty()) {
    return;
  }

  std::set<std::string> filter_set(filter.begin(), filter.end());
  // Only add filters not already added.
  for (auto it = filters_.begin(); it != filters_.end(); ++it) {
    if (filter_set.find(it->filter->pattern()) != filter_set.end()) {
      filter_set.erase(it->filter->pattern());
    }
  }

  zxdb::Job* default_job = session_->system().GetJobs()[0];

  if (!filter_set.empty() && !main_filter) {
    // We have an extra filter => wait for a main process to be started to start decoding events.
    decode_events_ = false;
  }

  for (const auto& pattern : filter_set) {
    filters_.push_back(
        ProcessFilter{.filter = session_->system().CreateNewFilter(), .main_filter = main_filter});
    filters_.back().filter->SetPattern(pattern);
    filters_.back().filter->SetJob(default_job);
  }
}

void InterceptionWorkflow::Launch(zxdb::Target* target, const std::vector<std::string>& command) {
  FX_CHECK(!command.empty()) << "No arguments passed to launcher";

  auto on_err = [this, command](const zxdb::Err& err) {
    std::string cmd;
    for (auto& param : command) {
      cmd.append(param);
      cmd.append(" ");
    }
    int64_t timestamp = time(nullptr);
    syscall_decoder_dispatcher()->AddProcessLaunchedEvent(
        std::make_shared<ProcessLaunchedEvent>(timestamp, cmd, err.ok() ? "" : err.msg()));
  };

  if (command[0] == "run") {
    // The component workflow.
    debug_ipc::LaunchRequest request;
    request.inferior_type = debug_ipc::InferiorType::kComponent;
    request.argv = std::vector<std::string>(command.begin() + 1, command.end());
    session_->remote_api()->Launch(
        request, [this, target = target->GetWeakPtr(), on_err = std::move(on_err)](
                     const zxdb::Err& err, debug_ipc::LaunchReply reply) {
          if (err.ok() && (reply.status != debug_ipc::kZxOk)) {
            zxdb::Err status_err(zxdb::ErrType::kGeneral, fidl_codec::StatusName(reply.status));
            on_err(status_err);
          } else {
            on_err(err);
          }
          target->session()->ExpectComponent(reply.component_id);
          if (target->GetProcess() != nullptr) {
            SetBreakpoints(target->GetProcess());
          }
        });
    return;
  }

  target->SetArgs(command);
  target->Launch(
      [this, on_err = std::move(on_err)](fxl::WeakPtr<zxdb::Target> target, const zxdb::Err& err) {
        on_err(err);
        if (target->GetProcess() != nullptr) {
          SetBreakpoints(target->GetProcess());
        }
      });
}

void InterceptionWorkflow::SetBreakpoints(zxdb::Process* process) {
  if (configured_processes_.find(process->GetKoid()) != configured_processes_.end()) {
    return;
  }

  bool main_process = false;
  for (const auto& filter : filters_) {
    if (process->GetName().find(filter.filter->pattern()) != std::string::npos) {
      main_process = filter.main_filter;
      break;
    }
  }

  if (main_process) {
    if (!decode_events_) {
      // One main process has started => start decoding events.
      decode_events_ = true;

      // Configure breakpoints for all the secondary processes already launched.
      for (const auto& configured_process : configured_processes_) {
        auto tmp = configured_process.second.process.get();
        if (tmp != nullptr) {
          DoSetBreakpoints(tmp);
        }
      }
    }
  }

  configured_processes_.emplace(
      std::pair(process->GetKoid(), ConfiguredProcess(process->GetWeakPtr(), main_process)));

  if (decode_events_) {
    DoSetBreakpoints(process);
  }
}

void InterceptionWorkflow::DoSetBreakpoints(zxdb::Process* zxdb_process) {
  int64_t timestamp = time(nullptr);
  Process* process = syscall_decoder_dispatcher()->SearchProcess(zxdb_process->GetKoid());
  if (process == nullptr) {
    process = syscall_decoder_dispatcher()->CreateProcess(
        zxdb_process->GetName(), zxdb_process->GetKoid(), zxdb_process->GetWeakPtr());
  }
  syscall_decoder_dispatcher()->AddProcessMonitoredEvent(
      std::make_shared<ProcessMonitoredEvent>(timestamp, process, ""));

  for (auto& syscall : syscall_decoder_dispatcher()->syscalls()) {
    bool put_breakpoint = true;
    if (!syscall.second->is_function()) {
      // Only apply the filters to syscalls. We always want to intercept regular
      // functions because they give us the information about the starting handles.
      if (!syscall_decoder_dispatcher()->decode_options().syscall_filters.empty()) {
        put_breakpoint = false;
        for (const auto& syscall_filter :
             syscall_decoder_dispatcher()->decode_options().syscall_filters) {
          if (syscall_filter->Matches(syscall.second->name())) {
            put_breakpoint = true;
            break;
          }
        }
      }
      if (put_breakpoint) {
        for (const auto& syscall_filter :
             syscall_decoder_dispatcher()->decode_options().exclude_syscall_filters) {
          if (syscall_filter->Matches(syscall.second->name())) {
            put_breakpoint = false;
            break;
          }
        }
      }
    }
    if (put_breakpoint) {
      zxdb::BreakpointSettings settings;
      settings.enabled = true;
      settings.name = syscall.second->name();
      settings.stop_mode = zxdb::BreakpointSettings::StopMode::kThread;
      settings.type = debug_ipc::BreakpointType::kSoftware;
      settings.scope = zxdb::ExecutionScope(zxdb_process->GetTarget());

      zxdb::Identifier identifier;
      zxdb::Err err =
          zxdb::ExprParser::ParseIdentifier(syscall.second->breakpoint_name(), &identifier);
      FX_CHECK(err.ok());
      settings.locations.emplace_back(std::move(identifier));

      zxdb::Breakpoint* breakpoint = session_->system().CreateNewBreakpoint();
      breakpoint->SetSettings(settings);
    }
  }
}

void InterceptionWorkflow::Go() {
  debug_ipc::MessageLoop* current = debug_ipc::MessageLoop::Current();
  current->Run();
  current->Cleanup();
}

namespace {

// Makes sure we never get stuck in the workflow at a breakpoint.
class AlwaysContinue {
 public:
  explicit AlwaysContinue(zxdb::Thread* thread) : thread_(thread) {}
  ~AlwaysContinue() { thread_->Continue(false); }

 private:
  zxdb::Thread* thread_;
};

}  // namespace

}  // namespace fidlcat
