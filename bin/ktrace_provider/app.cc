// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/ktrace_provider/app.h"

#include <fcntl.h>
#include <unistd.h>

#include "apps/tracing/lib/trace/provider.h"
#include "apps/tracing/src/ktrace_provider/importer.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

namespace ktrace_provider {
namespace {

constexpr char kDefaultProviderLabel[] = "ktrace";
constexpr char kCategory[] = "kernel";
constexpr char kDmctlDev[] = "/dev/class/misc/dmctl";
constexpr char kTraceDev[] = "/dev/class/misc/ktrace";
constexpr char kKTraceOff[] = "ktraceoff";
constexpr char kKTraceOn[] = "ktraceon";

}  // namespace

App::App(const ftl::CommandLine& command_line)
    : application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()),
      weak_ptr_factory_(this) {
  if (!tracing::InitializeTracerFromCommandLine(
          application_context_.get(), command_line, {kDefaultProviderLabel})) {
    FTL_LOG(ERROR) << "Failed to initialize trace provider.";
    exit(1);
  }

  trace_handler_key_ =
      tracing::writer::AddTraceHandler([weak = weak_ptr_factory_.GetWeakPtr()](
          tracing::writer::TraceState state) {
        if (weak)
          weak->UpdateState(state);
      });
}

App::~App() {
  tracing::writer::RemoveTraceHandler(trace_handler_key_);
  tracing::DestroyTracer();
}

void App::UpdateState(tracing::writer::TraceState state) {
  FTL_VLOG(1) << "UpdateState: state=" << static_cast<int>(state);
  switch (state) {
    case tracing::writer::TraceState::kStarted:
      if (tracing::writer::IsTracingEnabledForCategory(kCategory)) {
        RestartTracing();
      }
      break;
    case tracing::writer::TraceState::kStopping:
      if (trace_running_) {
        StopTracing();
        CollectTraces();
      }
      break;
    default:
      StopTracing();
      break;
  }
}

void App::RestartTracing() {
  SendDevMgrCommand(kKTraceOff);
  trace_running_ = SendDevMgrCommand(kKTraceOn);
}

void App::StopTracing() {
  if (trace_running_) {
    trace_running_ = false;
    SendDevMgrCommand(kKTraceOff);
  }
}

bool App::SendDevMgrCommand(std::string command) {
  int result = open(kDmctlDev, O_WRONLY);
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to open " << kDmctlDev << ": errno=" << errno;
    return false;
  }
  ftl::UniqueFD fd(result);  // take ownership here

  ssize_t actual = write(fd.get(), command.c_str(), command.size());
  if (actual < 0) {
    FTL_LOG(ERROR) << "Failed to write command to dmctl: errno=" << errno;
    return false;
  }

  return true;
}

void App::CollectTraces() {
  auto writer = tracing::writer::TraceWriter::Prepare();
  if (!writer)
    return;

  std::vector<uint8_t> buffer;
  if (!files::ReadFileToVector(kTraceDev, &buffer)) {
    FTL_LOG(ERROR) << "Failed to read traces from " << kTraceDev;
    return;
  }
  FTL_VLOG(2) << "Collected " << buffer.size() << " bytes of trace data";

  Importer importer(writer);
  if (!importer.Import(buffer.data(), buffer.size())) {
    FTL_LOG(ERROR) << "Errors encountered while importing ktrace data";
  }
}

}  // namespace ktrace_provider
