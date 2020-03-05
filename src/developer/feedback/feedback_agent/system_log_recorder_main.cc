// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <trace-provider/provider.h>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/feedback_agent/system_log_recorder.h"
#include "src/lib/syslog/cpp/logger.h"

feedback::FileSize MaxLogsSize();

int main(int argc, const char** argv) {
  syslog::InitLogger({"feedback"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), "feedback_agent_trace_provider");

  auto context = sys::ComponentContext::Create();

  feedback::SystemLogRecorder system_logs(context->svc(), feedback::kCurrentLogsFilePaths,
                                          MaxLogsSize());
  system_logs.StartRecording();

  loop.Run();

  return EXIT_SUCCESS;
}

feedback::FileSize MaxLogsSize() {
  using namespace feedback;

  return FileSize::Kilobytes(kPersistentLogsMaxSizeInKb);
}
