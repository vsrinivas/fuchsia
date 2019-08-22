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
    zxdb::Thread* thread, debug_ipc::NotifyException::Type type,
    const std::vector<fxl::WeakPtr<zxdb::Breakpoint>>& hit_breakpoints) {
  FXL_CHECK(thread) << "Internal error: Stopped in a breakpoint without a thread?";

  if (type == debug_ipc::NotifyException::Type::kGeneral) {
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
    if (settings.location.type == zxdb::InputLocation::Type::kSymbol &&
        settings.location.symbol.components().size() == 1u) {
      threads_in_error_.erase(thread->GetKoid());
      for (auto& syscall : workflow_->syscall_decoder_dispatcher()->syscalls()) {
        if (settings.location.symbol.components()[0].name() == syscall->breakpoint_name()) {
          workflow_->syscall_decoder_dispatcher()->DecodeSyscall(this, thread, syscall.get());
          return;
        }
      }
      FXL_LOG(INFO) << "Internal error: breakpoint "
                    << settings.location.symbol.components()[0].name() << " not managed";
      thread->Continue();
      return;
    }
  }
  if (threads_in_error_.find(thread->GetKoid()) == threads_in_error_.end()) {
    FXL_LOG(INFO) << "Internal error: Thread " << thread->GetKoid()
                  << " stopped on exception with no breakpoint set";
    threads_in_error_.emplace(thread->GetKoid());
  }
  thread->Continue();
}

void InterceptingThreadObserver::Register(int64_t koid, SyscallDecoder* decoder) {
  breakpoint_map_[koid] = decoder;
}

void InterceptingThreadObserver::CreateNewBreakpoint(zxdb::BreakpointSettings& settings) {
  zxdb::Breakpoint* breakpoint = workflow_->session_->system().CreateNewBreakpoint();

  breakpoint->SetSettings(settings, [](const zxdb::Err& err) {
    if (!err.ok()) {
      FXL_LOG(INFO) << "Error in setting breakpoint: " << err.msg();
    }
  });
}

void InterceptingTargetObserver::DidCreateProcess(zxdb::Target* target, zxdb::Process* process,
                                                  bool /*autoattached_to_new_process*/) {
  process->AddObserver(&dispatcher_);
  workflow_->syscall_decoder_dispatcher()->AddLaunchedProcess(process->GetKoid());
  workflow_->SetBreakpoints(target);
  // Register a new target to be able to attach to more processes.
  zxdb::Target* next_target = workflow_->GetNewTarget();
  workflow_->AddObserver(next_target);
}

void InterceptingTargetObserver::WillDestroyProcess(zxdb::Target* /*target*/,
                                                    zxdb::Process* process, DestroyReason reason,
                                                    int /*exit_code*/) {
  std::string action;
  switch (reason) {
    case zxdb::TargetObserver::DestroyReason::kExit:
      action = "exited";
      break;
    case zxdb::TargetObserver::DestroyReason::kDetach:
      action = "detached";
      break;
    case zxdb::TargetObserver::DestroyReason::kKill:
      action = "killed";
      break;
    default:
      action = "detached for unknown reason";
      break;
  }
  FXL_LOG(INFO) << "Process " << process->GetKoid() << " " << action;
  workflow_->ProcessDetached(process->GetKoid());
}

InterceptionWorkflow::InterceptionWorkflow()
    : session_(new zxdb::Session()),
      delete_session_(true),
      loop_(new debug_ipc::PlatformMessageLoop()),
      delete_loop_(true),
      observer_(this) {}

InterceptionWorkflow::InterceptionWorkflow(zxdb::Session* session,
                                           debug_ipc::PlatformMessageLoop* loop)
    : session_(session),
      delete_session_(false),
      loop_(loop),
      delete_loop_(false),
      observer_(this) {}

InterceptionWorkflow::~InterceptionWorkflow() {
  if (delete_session_) {
    delete session_;
  }
  if (delete_loop_) {
    delete loop_;
  }
}

void InterceptionWorkflow::Initialize(
    const std::vector<std::string>& symbol_paths,
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

  // Adding it to the settings will trigger the loading of the symbols.
  // Redundant adds are ignored.
  session_->system().settings().SetList(zxdb::ClientSettings::System::kSymbolPaths,
                                        std::move(paths));

  // 2) Ensure that the session correctly reads data off of the loop.
  buffer_.set_data_available_callback([this]() { session_->OnStreamReadable(); });

  // 3) Provide a loop, if none exists.
  if (debug_ipc::MessageLoop::Current() == nullptr) {
    loop_->Init();
  }
}

void InterceptionWorkflow::Connect(const std::string& host, uint16_t port,
                                   const SimpleErrorFunction& and_then) {
  session_->Connect(host, port, [and_then](const zxdb::Err& err) { and_then(err); });
}

// Helper function that finds a target for fidlcat to attach itself to. The
// target with |process_koid| must already be running.
zxdb::Target* InterceptionWorkflow::GetTarget(uint64_t process_koid) {
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

// Gets the workflow to observe the given target.
void InterceptionWorkflow::AddObserver(zxdb::Target* target) { target->AddObserver(&observer_); }

void InterceptionWorkflow::Attach(const std::vector<uint64_t>& process_koids) {
  for (uint64_t process_koid : process_koids) {
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
        FXL_LOG(INFO) << "Unable to attach to koid " << process_koid << ": " << err.msg();
        return;
      }

      FXL_LOG(INFO) << "Attached to process with koid " << process_koid;
      target->GetProcess()->AddObserver(&observer_.process_observer());
      SetBreakpoints(target);
    });
  }
}

void InterceptionWorkflow::ProcessDetached(uint64_t koid) {
  configured_processes_.erase(koid);
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

  auto on_err = [command](const zxdb::Err& err) {
    std::string cmd;
    for (auto& param : command) {
      cmd.append(param);
      cmd.append(" ");
    }
    if (!err.ok()) {
      FXL_LOG(INFO) << "Unable to launch " << cmd << ": " << err.msg();
    } else {
      FXL_LOG(INFO) << "Launched " << cmd;
    }
    return err.ok();
  };

  if (command[0] == "run") {
    // The component workflow.
    debug_ipc::LaunchRequest request;
    request.inferior_type = debug_ipc::InferiorType::kComponent;
    request.argv = std::vector<std::string>(command.begin() + 1, command.end());
    session_->remote_api()->Launch(
        request, [this, target = target->GetWeakPtr(), on_err = std::move(on_err)](
                     const zxdb::Err& err, debug_ipc::LaunchReply reply) {
          if (!on_err(err)) {
            return;
          }
          if (reply.status != debug_ipc::kZxOk) {
            FXL_LOG(INFO) << "Could not start component " << reply.process_name << ": "
                          << DisplayStatus(reply.status);
          }
          target->session()->ExpectComponent(reply.component_id);
          if (target->GetProcess() != nullptr) {
            SetBreakpoints(target.get());
          }
        });
    return;
  }

  target->SetArgs(command);
  target->Launch(
      [this, on_err = std::move(on_err)](fxl::WeakPtr<zxdb::Target> target, const zxdb::Err& err) {
        if (!on_err(err)) {
          return;
        }
        if (target->GetProcess() != nullptr) {
          SetBreakpoints(target.get());
        }
      });
}

void InterceptionWorkflow::SetBreakpoints(zxdb::Target* target) {
  if (configured_processes_.find(target->GetProcess()->GetKoid()) != configured_processes_.end()) {
    return;
  }
  configured_processes_.emplace(target->GetProcess()->GetKoid());
  for (auto& syscall : syscall_decoder_dispatcher()->syscalls()) {
    bool put_breakpoint = true;
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
      if (put_breakpoint) {
        zxdb::BreakpointSettings settings;
        settings.enabled = true;
        settings.name = syscall->name();
        settings.stop_mode = zxdb::BreakpointSettings::StopMode::kThread;
        settings.type = debug_ipc::BreakpointType::kSoftware;
        settings.location.symbol = zxdb::Identifier(syscall->breakpoint_name());
        settings.location.type = zxdb::InputLocation::Type::kSymbol;
        settings.scope = zxdb::BreakpointSettings::Scope::kTarget;
        settings.scope_target = target;

        zxdb::Breakpoint* breakpoint = session_->system().CreateNewBreakpoint();

        breakpoint->SetSettings(settings, [](const zxdb::Err& err) {
          if (!err.ok()) {
            FXL_LOG(INFO) << "Error in setting breakpoints: " << err.msg();
          }
        });
      }
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
