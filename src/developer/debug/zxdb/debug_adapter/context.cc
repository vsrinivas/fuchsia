// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "context.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_attach.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_breakpoint.h"
#include "src/developer/debug/zxdb/debug_adapter/server.h"

namespace zxdb {

DebugAdapterContext::DebugAdapterContext(Session *session, debug_ipc::StreamBuffer *stream)
    : session_(session), dap_(dap::Session::create()) {
  reader_ = std::make_shared<DebugAdapterReader>(stream);
  writer_ = std::make_shared<DebugAdapterWriter>(stream);

  InitDebugAdapterProtocolSession();
}

DebugAdapterContext::~DebugAdapterContext() {
  if (observers_added_) {
    session()->thread_observers().RemoveObserver(this);
  }
}

void DebugAdapterContext::InitDebugAdapterProtocolSession() {
  dap_->registerHandler([](const dap::DisconnectRequest &req) {
    DEBUG_LOG(DebugAdapter) << "DisconnectRequest received";
    return dap::DisconnectResponse();
  });

  dap_->registerHandler([&](const dap::InitializeRequest &req) {
    DEBUG_LOG(DebugAdapter) << "InitializeRequest received";
    dap::InitializeResponse response;
    response.supportsFunctionBreakpoints = false;
    response.supportsConfigurationDoneRequest = true;
    response.supportsEvaluateForHovers = true;
    if (req.supportsInvalidatedEvent) {
      this->supports_invalidate_event_ = req.supportsInvalidatedEvent.value();
    }
    return response;
  });

  dap_->registerHandler([&](const dap::LaunchRequest &req) {
    DEBUG_LOG(DebugAdapter) << "LaunchRequest received";
    dap::LaunchResponse response;
    return response;
  });

  dap_->registerSentHandler([&](const dap::ResponseOrError<dap::InitializeResponse> &response) {
    DEBUG_LOG(DebugAdapter) << "InitializeResponse sent";
    // Subscribe to session events now. All messages should be sent only after Initialize response
    // is sent. Subscribing earlier would lead to events being sent before Initialize request is
    // processed.
    session()->thread_observers().AddObserver(this);
    observers_added_ = true;
    dap_->send(dap::InitializedEvent());
  });

  dap_->registerHandler([](const dap::SetExceptionBreakpointsRequest &req) {
    DEBUG_LOG(DebugAdapter) << "SetExceptionBreakpointsRequest received";
    dap::SetExceptionBreakpointsResponse response;
    return response;
  });

  dap_->registerHandler([&](const dap::SetBreakpointsRequest &req)
                            -> dap::ResponseOrError<dap::SetBreakpointsResponse> {
    DEBUG_LOG(DebugAdapter) << "SetBreakpointsRequest received";
    return OnRequestBreakpoint(this, req);
  });

  dap_->registerHandler([=](const dap::ConfigurationDoneRequest &req) {
    DEBUG_LOG(DebugAdapter) << "ConfigurationDoneRequest received";
    return dap::ConfigurationDoneResponse();
  });

  dap_->registerHandler(
      [&](const dap::AttachRequestZxdb &req) -> dap::ResponseOrError<dap::AttachResponse> {
        DEBUG_LOG(DebugAdapter) << "AttachRequest received";
        return OnRequestAttach(this, req);
      });

  dap_->onError([&](const char *msg) { FX_LOGS(ERROR) << "dap::Session error:" << msg << "\r\n"; });

  dap_->connect(reader_, writer_);
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

}  // namespace zxdb
