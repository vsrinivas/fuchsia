// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interception_workflow.h"

#include <cstring>
#include <regex>
#include <string>
#include <thread>

#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/thread.h"

// TODO(fidlcat): Look into this.  Removing the hack that led to this (in
// debug_ipc/helper/message_loop.h) seems to work, except it breaks SDK builds
// on CQ in a way I can't repro locally.
#undef __TA_REQUIRES

#include "tools/fidlcat/lib/decode_options.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

void InterceptingThreadObserver::OnThreadStopped(
    zxdb::Thread* thread, debug_ipc::ExceptionType type,
    const std::vector<fxl::WeakPtr<zxdb::Breakpoint>>& hit_breakpoints) {
  FXL_CHECK(thread) << "Internal error: Stopped in a breakpoint without a thread?";

  if (type != debug_ipc::ExceptionType::kSoftware) {
    FXL_CHECK(hit_breakpoints.empty());
    if (threads_in_error_.find(thread->GetKoid()) == threads_in_error_.end()) {
      threads_in_error_.emplace(thread->GetKoid());
      workflow_->syscall_decoder_dispatcher()->DecodeException(workflow_, thread);
    }
    return;
  }

  if (hit_breakpoints.empty()) {
    // This can happen when we are shutting down fidlcat.
    // There is nothing to do => we just return.
    return;
  }

  FXL_CHECK(hit_breakpoints.size() == 1)
      << "Internal error: more than one simultaneous breakpoint for thread " << thread->GetKoid();

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
  for (auto& bp_ptr : hit_breakpoints) {
    zxdb::BreakpointSettings settings = bp_ptr->GetSettings();
    if (settings.locations.size() == 1u &&
        settings.locations[0].type == zxdb::InputLocation::Type::kName &&
        settings.locations[0].name.components().size() == 1u) {
      threads_in_error_.erase(thread->GetKoid());
      for (auto& syscall : workflow_->syscall_decoder_dispatcher()->syscalls()) {
        if (settings.locations[0].name.components()[0].name() == syscall->breakpoint_name()) {
          workflow_->syscall_decoder_dispatcher()->DecodeSyscall(this, thread, syscall.get());
          return;
        }
      }
      FXL_LOG(ERROR) << thread->GetProcess()->GetName() << ' ' << thread->GetProcess()->GetKoid()
                     << ':' << thread->GetKoid() << ": Internal error: breakpoint "
                     << settings.locations[0].name.components()[0].name() << " not managed";
      thread->Continue();
      return;
    }
  }
  thread->Continue();
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

  FXL_VLOG(2) << "Thread " << thread->GetKoid() << ": creating return value breakpoint for "
              << syscall_name << " at address " << std::hex << address << std::dec;
  CreateNewBreakpoint(thread, settings);
}

void InterceptingThreadObserver::CreateNewBreakpoint(zxdb::Thread* thread,
                                                     zxdb::BreakpointSettings& settings) {
  zxdb::Breakpoint* breakpoint = workflow_->session_->system().CreateNewBreakpoint();

  breakpoint->SetSettings(settings, [thread](const zxdb::Err& err) {
    if (!err.ok()) {
      FXL_LOG(ERROR) << thread->GetProcess()->GetName() << ' ' << thread->GetProcess()->GetKoid()
                     << ':' << thread->GetKoid() << ": Error in setting breakpoint: " << err.msg();
    }
  });
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

InterceptionWorkflow::InterceptionWorkflow(zxdb::Session* session,
                                           debug_ipc::MessageLoop* loop)
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
    const std::vector<std::string>& symbol_paths, const std::vector<std::string>& symbol_repo_paths,
    const std::string& symbol_cache_path, const std::vector<std::string>& symbol_servers,
    std::unique_ptr<SyscallDecoderDispatcher> syscall_decoder_dispatcher) {
  syscall_decoder_dispatcher_ = std::move(syscall_decoder_dispatcher);
  // 1) Set up symbol index.

  // Stolen from console/console_main.cc
  std::vector<std::string> paths;

  // At this moment, the build index has all the "default" paths.
  zxdb::BuildIDIndex& build_id_index = session_->system().GetSymbols()->build_id_index();

  for (const auto& build_id_file : build_id_index.build_id_files()) {
    paths.push_back(build_id_file);
  }
  for (const auto& source : build_id_index.sources()) {
    paths.push_back(source);
  }

  // We add the options paths given paths.
  paths.insert(paths.end(), symbol_paths.begin(), symbol_paths.end());

  if (!symbol_cache_path.empty()) {
    session_->system().settings().SetString(zxdb::ClientSettings::System::kSymbolCache,
                                            symbol_cache_path);
  }

  // Adding it to the settings will trigger the loading of the symbols.
  // Redundant adds are ignored.
  session_->system().settings().SetList(zxdb::ClientSettings::System::kSymbolPaths,
                                        std::move(paths));

  if (!symbol_repo_paths.empty()) {
    session_->system().settings().SetList(zxdb::ClientSettings::System::kSymbolRepoPaths,
                                          symbol_repo_paths);
  }

  // 2) Ensure that the session correctly reads data off of the loop.
  buffer_.set_data_available_callback([this]() { session_->OnStreamReadable(); });

  // 3) Provide a loop, if none exists.
  if (debug_ipc::MessageLoop::Current() == nullptr) {
    std::string error_message;
    bool success = loop_->Init(&error_message);
    FXL_CHECK(success) << error_message;
  }

  // 4) Initialize the symbol servers.
  if (!symbol_servers.empty()) {
    session_->system().settings().SetList(zxdb::ClientSettings::System::kSymbolServers,
                                          symbol_servers);
  }
}

void InterceptionWorkflow::Connect(const std::string& host, uint16_t port,
                                   const SimpleErrorFunction& and_then) {
  session_->Connect(host, port, [and_then](const zxdb::Err& err) { and_then(err); });
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
      FXL_CHECK(target->GetProcess()->GetKoid() == process_koid)
          << "Internal error: target attached to wrong process";
      continue;
    }

    // The debugger is not yet attached to the process.  Attach to it.
    target->Attach(process_koid, [this, target, process_koid](fxl::WeakPtr<zxdb::Target> /*target*/,
                                                              const zxdb::Err& err) {
      if (!err.ok()) {
        syscall_decoder_dispatcher()->ProcessMonitored("", process_koid, err.msg());
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
  syscall_decoder_dispatcher()->StopMonitoring(koid);
  Detach();
}

void InterceptionWorkflow::Detach() {
  if (configured_processes_.empty()) {
    if (!shutdown_done_) {
      shutdown_done_ = true;
      Shutdown();
    }
  }
}

void InterceptionWorkflow::Filter(const std::vector<std::string>& filter) {
  std::set<std::string> filter_set(filter.begin(), filter.end());

  for (auto it = filters_.begin(); it != filters_.end();) {
    if (filter_set.find((*it)->pattern()) != filter_set.end()) {
      filter_set.erase((*it)->pattern());
      ++it;
    } else {
      session_->system().DeleteFilter(*it);
      it = filters_.erase(it);
    }
  }

  zxdb::JobContext* default_job = session_->system().GetJobContexts()[0];

  for (const auto& pattern : filter_set) {
    filters_.push_back(session_->system().CreateNewFilter());
    filters_.back()->SetPattern(pattern);
    filters_.back()->SetJob(default_job);
  }
}

void InterceptionWorkflow::Launch(zxdb::Target* target, const std::vector<std::string>& command) {
  FXL_CHECK(!command.empty()) << "No arguments passed to launcher";

  auto on_err = [this, command](const zxdb::Err& err) {
    std::string cmd;
    for (auto& param : command) {
      cmd.append(param);
      cmd.append(" ");
    }
    syscall_decoder_dispatcher()->ProcessLaunched(cmd, err.ok() ? "" : err.msg());
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
            std::stringstream status;
            StatusName(reply.status, status);
            zxdb::Err status_err(zxdb::ErrType::kGeneral, status.str());
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
  configured_processes_.emplace(process->GetKoid());

  syscall_decoder_dispatcher()->ProcessMonitored(process->GetName(), process->GetKoid(), "");

  for (auto& syscall : syscall_decoder_dispatcher()->syscalls()) {
    bool put_breakpoint = true;
    if (!syscall->is_function()) {
      // Only apply the filters to syscalls. We always want to intercept regular
      // functions because they give us the information about the starting handles.
      if (!syscall_decoder_dispatcher()->decode_options().syscall_filters.empty()) {
        put_breakpoint = false;
        for (const auto& syscall_filter :
             syscall_decoder_dispatcher()->decode_options().syscall_filters) {
          if (regex_match(syscall->name(), syscall_filter)) {
            put_breakpoint = true;
            break;
          }
        }
      }
      if (put_breakpoint) {
        for (const auto& syscall_filter :
             syscall_decoder_dispatcher()->decode_options().exclude_syscall_filters) {
          if (regex_match(syscall->name(), syscall_filter)) {
            put_breakpoint = false;
            break;
          }
        }
      }
    }
    if (put_breakpoint) {
      zxdb::BreakpointSettings settings;
      settings.enabled = true;
      settings.name = syscall->name();
      settings.stop_mode = zxdb::BreakpointSettings::StopMode::kThread;
      settings.type = debug_ipc::BreakpointType::kSoftware;
      settings.locations.emplace_back(zxdb::Identifier(syscall->breakpoint_name()));
      settings.scope = zxdb::ExecutionScope(process->GetTarget());

      zxdb::Breakpoint* breakpoint = session_->system().CreateNewBreakpoint();

      breakpoint->SetSettings(settings, [](const zxdb::Err& err) {
        if (!err.ok()) {
          FXL_LOG(INFO) << "Error in setting breakpoints: " << err.msg();
        }
      });
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
  ~AlwaysContinue() { thread_->Continue(); }

 private:
  zxdb::Thread* thread_;
};

}  // namespace

}  // namespace fidlcat
