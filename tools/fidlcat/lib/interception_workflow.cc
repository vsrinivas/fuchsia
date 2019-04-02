// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interception_workflow.h"

#include <string>
#include <thread>

#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace fidlcat {

const char InterceptionWorkflow::kZxChannelWriteName[] = "zx_channel_write@plt";

namespace internal {

void InterceptingThreadObserver::OnThreadStopped(
    zxdb::Thread* thread, debug_ipc::NotifyException::Type type,
    const std::vector<fxl::WeakPtr<zxdb::Breakpoint>>& hit_breakpoints) {
  for (auto& bp_ptr : hit_breakpoints) {
    zxdb::BreakpointSettings settings = bp_ptr->GetSettings();
    if (settings.location.type == zxdb::InputLocation::Type::kSymbol &&
        settings.location.symbol.size() == 1u &&
        settings.location.symbol[0] ==
            InterceptionWorkflow::kZxChannelWriteName) {
      workflow_->OnZxChannelWrite(thread);
    }
  }
}

}  // namespace internal

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
    const std::vector<std::string>& symbol_paths) {
  //// Set up symbol index.

  // Stolen from console/console_main.cc
  std::vector<std::string> paths;

  // At this moment, the build index has all the "default" paths.
  zxdb::BuildIDIndex& build_id_index =
      session_->system().GetSymbols()->build_id_index();

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
  session_->system().settings().SetList(
      zxdb::ClientSettings::System::kSymbolPaths, std::move(paths));

  //// Ensure that the session correctly reads data off of the loop.
  buffer_.set_data_available_callback(
      [this]() { session_->OnStreamReadable(); });

  //// Provide a loop, if none exists.
  if (debug_ipc::MessageLoop::Current() == nullptr) {
    loop_->Init();
  }
}

void InterceptionWorkflow::Connect(const std::string& host, uint16_t port,
                                   SimpleErrorFunction and_then) {
  session_->Connect(host, port,
                    [and_then](const zxdb::Err& err) { and_then(err); });
}

void InterceptionWorkflow::Attach(uint64_t process_koid,
                                  SimpleErrorFunction and_then) {
  zxdb::Target* target = nullptr;
  for (zxdb::Target* t : session_->system().GetTargets()) {
    if (t->GetProcess() && t->GetProcess()->GetKoid() == process_koid) {
      return;
    }
  }
  if (target == nullptr) {
    for (zxdb::Target* t : session_->system().GetTargets()) {
      if (t->GetState() == zxdb::Target::State::kNone) {
        target = t;
      }
    }
  }
  if (target == nullptr) {
    target = session_->system().CreateNewTarget(nullptr);
  }
  // TODO: Remove observer when appropriate.
  target->AddObserver(&observer_);
  target->Attach(process_koid, [and_then = std::move(and_then)](
                                   fxl::WeakPtr<zxdb::Target>,
                                   const zxdb::Err& err) { and_then(err); });
}

void InterceptionWorkflow::SetBreakpoints(SimpleErrorFunction and_then_each) {
  zxdb::Err err;
  for (zxdb::Target* target : session_->system().GetTargets()) {
    // Set the breakpoint
    zxdb::BreakpointSettings settings;
    settings.enabled = true;
    settings.stop_mode = zxdb::BreakpointSettings::StopMode::kThread;
    settings.type = debug_ipc::BreakpointType::kSoftware;
    settings.location.symbol = {kZxChannelWriteName};
    settings.location.type = zxdb::InputLocation::Type::kSymbol;
    settings.scope = zxdb::BreakpointSettings::Scope::kTarget;
    settings.scope_target = target;

    zxdb::Breakpoint* breakpoint = session_->system().CreateNewBreakpoint();

    breakpoint->SetSettings(
        settings, [breakpoint = breakpoint->GetWeakPtr(), and_then_each](
                      const zxdb::Err& err) { and_then_each(err); });
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
  AlwaysContinue(zxdb::Thread* thread) : thread_(thread) {}
  ~AlwaysContinue() { thread_->Continue(); }

 private:
  zxdb::Thread* thread_;
};

}  // namespace

// The workflow for zx_channel_write.  Read the registers, read the associated
// memory, pass it to the callback to do the user-facing thing.
void InterceptionWorkflow::OnZxChannelWrite(zxdb::Thread* thread) {
  std::vector<debug_ipc::RegisterCategory::Type> register_types = {
      debug_ipc::RegisterCategory::Type::kGeneral};

  thread->ReadRegisters(register_types, [this,
                                         thread_weak = thread->GetWeakPtr()](
                                            const zxdb::Err& err,
                                            const zxdb::RegisterSet& in_regs) {
    if (!thread_weak) {
      zxdb::Err e(zxdb::ErrType::kGeneral,
                  "Error reading registers: thread went away");
      ZxChannelWriteParams params;
      zx_channel_write_callback_(e, params);
    }
    if (!err.ok()) {
      AlwaysContinue ac(thread_weak.get());
      ZxChannelWriteParams params;
      zx_channel_write_callback_(
          zxdb::Err(err.type(), "Error reading registers" + err.msg()), params);
    }
    ZxChannelWriteParams::BuildZxChannelWriteParamsAndContinue(
        thread_weak, in_regs,
        [this, thread_weak](const zxdb::Err& err,
                            const ZxChannelWriteParams& params) {
          if (!thread_weak) {
            zxdb::Err e(
                zxdb::ErrType::kGeneral,
                "Error constructing zx_channel_write data: thread went away");
            ZxChannelWriteParams params;
            zx_channel_write_callback_(e, params);
          }
          AlwaysContinue ac(thread_weak.get());
          zx_channel_write_callback_(err, params);
        });
  });

#if 0
  for (uint32_t i = 0; i < params.GetNumBytes(); i++) {
    fprintf(stderr, "%d %c\n", params.GetBytes().get()[i],
            params.GetBytes().get()[i]);
  }
#endif
}

}  // namespace fidlcat
