// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "context.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_attach.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_breakpoint.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_continue.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_launch.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_pause.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_threads.h"
#include "src/developer/debug/zxdb/debug_adapter/server.h"

namespace zxdb {

DebugAdapterContext::DebugAdapterContext(Session *session, debug_ipc::StreamBuffer *stream)
    : session_(session), dap_(dap::Session::create()) {
  reader_ = std::make_shared<DebugAdapterReader>(stream);
  writer_ = std::make_shared<DebugAdapterWriter>(stream);

  dap_->registerHandler([this](const dap::InitializeRequest &req) {
    DEBUG_LOG(DebugAdapter) << "InitializeRequest received";
    dap::InitializeResponse response;
    response.supportsFunctionBreakpoints = false;
    response.supportsConfigurationDoneRequest = true;
    response.supportsEvaluateForHovers = true;
    if (req.supportsInvalidatedEvent) {
      this->supports_invalidate_event_ = req.supportsInvalidatedEvent.value();
    }
    if (req.supportsRunInTerminalRequest) {
      this->supports_run_in_terminal_ = req.supportsRunInTerminalRequest.value();
    }
    return response;
  });

  dap_->registerSentHandler([this](const dap::ResponseOrError<dap::InitializeResponse> &response) {
    DEBUG_LOG(DebugAdapter) << "InitializeResponse sent";
    // Set up events and handlers now. All messages should be sent only after Initialize response
    // is sent. Setting up earlier would lead to events and responses being sent before Initialize
    // request is processed.
    Init();
    dap_->send(dap::InitializedEvent());
  });

  dap_->onError([](const char *msg) { FX_LOGS(ERROR) << "dap::Session error:" << msg << "\r\n"; });

  dap_->connect(reader_, writer_);
}

DebugAdapterContext::~DebugAdapterContext() {
  if (init_done_) {
    session()->thread_observers().RemoveObserver(this);
    session()->process_observers().RemoveObserver(this);
  }
}

void DebugAdapterContext::Init() {
  // Register handlers with dap module.
  dap_->registerHandler([](const dap::DisconnectRequest &req) {
    DEBUG_LOG(DebugAdapter) << "DisconnectRequest received";
    return dap::DisconnectResponse();
  });

  dap_->registerHandler([this](const dap::LaunchRequestZxdb &req) {
    DEBUG_LOG(DebugAdapter) << "LaunchRequest received";
    return OnRequestLaunch(this, req);
  });

  dap_->registerHandler([](const dap::SetExceptionBreakpointsRequest &req) {
    DEBUG_LOG(DebugAdapter) << "SetExceptionBreakpointsRequest received";
    dap::SetExceptionBreakpointsResponse response;
    return response;
  });

  dap_->registerHandler([this](const dap::SetBreakpointsRequest &req) {
    DEBUG_LOG(DebugAdapter) << "SetBreakpointsRequest received";
    return OnRequestBreakpoint(this, req);
  });

  dap_->registerHandler([](const dap::ConfigurationDoneRequest &req) {
    DEBUG_LOG(DebugAdapter) << "ConfigurationDoneRequest received";
    return dap::ConfigurationDoneResponse();
  });

  dap_->registerHandler([this](const dap::AttachRequestZxdb &req) {
    DEBUG_LOG(DebugAdapter) << "AttachRequest received";
    return OnRequestAttach(this, req);
  });

  dap_->registerHandler([this](const dap::ThreadsRequest &req) {
    DEBUG_LOG(DebugAdapter) << "ThreadRequest received";
    return OnRequestThreads(this, req);
  });

  dap_->registerHandler(
      [this](const dap::PauseRequest &req,
             std::function<void(dap::ResponseOrError<dap::PauseResponse>)> callback) {
        DEBUG_LOG(DebugAdapter) << "PauseRequest received";
        OnRequestPause(this, req, callback);
      });

  dap_->registerHandler([this](const dap::ContinueRequest &req) {
    DEBUG_LOG(DebugAdapter) << "ContinueRequest received";
    return OnRequestContinue(this, req);
  });

  // Register to zxdb session events
  session()->thread_observers().AddObserver(this);
  session()->process_observers().AddObserver(this);

  init_done_ = true;
}

void DebugAdapterContext::OnStreamReadable() {
  auto payload = dap_->getPayload();
  if (payload) {
    payload();
  }
}

void DebugAdapterContext::DidCreateThread(Thread *thread) {
  dap::ThreadEvent event;
  event.reason = "started";
  event.threadId = thread->GetKoid();
  dap_->send(event);
}

void DebugAdapterContext::WillDestroyThread(Thread *thread) {
  dap::ThreadEvent event;
  event.reason = "exited";
  event.threadId = thread->GetKoid();
  dap_->send(event);
}

void DebugAdapterContext::OnThreadStopped(Thread *thread, const StopInfo &info) {
  dap::StoppedEvent event;
  switch (info.exception_type) {
    case debug_ipc::ExceptionType::kSoftwareBreakpoint:
    case debug_ipc::ExceptionType::kHardwareBreakpoint:
      event.reason = "breakpoint";
      break;
    case debug_ipc::ExceptionType::kSingleStep:
      event.reason = "step";
      break;
    case debug_ipc::ExceptionType::kPolicyError:
      event.reason = "exception";
      event.description = "Policy error";
      break;
    case debug_ipc::ExceptionType::kPageFault:
      event.reason = "exception";
      event.description = "Page fault";
      break;
    case debug_ipc::ExceptionType::kUndefinedInstruction:
      event.reason = "exception";
      event.description = "Undefined Instruction";
      break;
    case debug_ipc::ExceptionType::kUnalignedAccess:
      event.reason = "exception";
      event.description = "Unaligned Access";
      break;
    default:
      event.reason = "unknown";
  }
  event.threadId = thread->GetKoid();
  dap_->send(event);
}

void DebugAdapterContext::OnThreadFramesInvalidated(Thread *thread) {
  if (supports_invalidate_event_) {
    dap::InvalidatedEvent event;
    event.threadId = thread->GetKoid();
    dap_->send(event);
  }
}

void DebugAdapterContext::DidCreateProcess(Process *process, bool autoattached_to_new_process,
                                           uint64_t timestamp) {
  dap::ProcessEvent event;
  event.name = process->GetName();
  event.isLocalProcess = false;

  switch (process->start_type()) {
    case Process::StartType::kAttach:
      event.startMethod = "attach";
      break;
    case Process::StartType::kComponent:
    case Process::StartType::kLaunch:
      event.startMethod = "launch";
      break;
  }

  bool pause_on_attach =
      session()->system().settings().GetBool(ClientSettings::System::kPauseOnAttach);
  if (autoattached_to_new_process && pause_on_attach) {
    event.startMethod = "attachForSuspendedLaunch";
  }

  dap_->send(event);
}

Target *DebugAdapterContext::GetCurrentTarget() {
  auto targets = session()->system().GetTargets();
  if (targets.size() > 0) {
    // Currently debug adapter supports only one target. The default target is used to attach
    // process.
    return targets[0];
  }
  return nullptr;
}

Process *DebugAdapterContext::GetCurrentProcess() {
  auto target = GetCurrentTarget();
  if (target) {
    return target->GetProcess();
  }
  return nullptr;
}

Thread *DebugAdapterContext::GetThread(uint64_t koid) {
  Thread *match = nullptr;
  auto process = GetCurrentProcess();
  if (process) {
    auto threads = process->GetThreads();
    for (auto t : threads) {
      if (koid == t->GetKoid()) {
        match = t;
        break;
      }
    }
  }
  return match;
}

}  // namespace zxdb
