// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics_service.h"

#include <lib/backtrace-request/backtrace-request.h>

#include "log.h"

namespace network {

namespace {
void TriggerBacktrace() {
  LOG_WARN("Requesting a backtrace from diagnostics, service. This is not a crash.");
  backtrace_request();
}
}  // namespace

DiagnosticsService::DiagnosticsService()
    : loop_(&kAsyncLoopConfigNeverAttachToThread), trigger_stack_trace_(TriggerBacktrace) {}

void DiagnosticsService::LogDebugInfoToSyslog(LogDebugInfoToSyslogRequestView _request,
                                              LogDebugInfoToSyslogCompleter::Sync& completer) {
  trigger_stack_trace_();
  completer.Reply();
}

void DiagnosticsService::Bind(fidl::ServerEnd<netdev::Diagnostics> server_end) {
  if (!thread_started_.exchange(true)) {
    // Lazily start a thread to serve diagnostics.
    loop_.StartThread("network-device-diagnostics");
  }
  fidl::BindServer(loop_.dispatcher(), std::move(server_end), this);
}

}  // namespace network
