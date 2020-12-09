// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "context.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_breakpoint.h"
#include "src/developer/debug/zxdb/debug_adapter/server.h"

namespace zxdb {

DebugAdapterContext::DebugAdapterContext(Session *session, debug_ipc::StreamBuffer *stream)
    : session_(session), dap_(dap::Session::create()) {
  reader_ = std::make_shared<DebugAdapterReader>(stream);
  writer_ = std::make_shared<DebugAdapterWriter>(stream);

  dap_->registerHandler([](const dap::DisconnectRequest &req) {
    DEBUG_LOG(DebugAdapter) << "DisconnectRequest received";
    return dap::DisconnectResponse();
  });

  dap_->registerHandler([&](const dap::InitializeRequest &req) {
    DEBUG_LOG(DebugAdapter) << "InitializeRequest received";
    dap::InitializeResponse response;
    response.supportsFunctionBreakpoints = true;
    response.supportsConfigurationDoneRequest = true;
    response.supportsEvaluateForHovers = true;
    return response;
  });

  dap_->registerHandler([&](const dap::LaunchRequest &req) {
    DEBUG_LOG(DebugAdapter) << "LaunchRequest received";
    dap::LaunchResponse response;
    return response;
  });

  dap_->registerSentHandler([&](const dap::ResponseOrError<dap::InitializeResponse> &response) {
    DEBUG_LOG(DebugAdapter) << "InitializeResponse sent";
    ;
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

  dap_->onError([&](const char *msg) { FX_LOGS(ERROR) << "dap::Session error:" << msg << "\r\n"; });

  dap_->connect(reader_, writer_);
}

void DebugAdapterContext::OnStreamReadable() {
  auto payload = dap_->getPayload();
  if (payload) {
    payload();
  }
}

}  // namespace zxdb
