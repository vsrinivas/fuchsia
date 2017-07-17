// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/ktrace_provider/app.h"

#include <fcntl.h>
#include <unistd.h>

#include <magenta/syscalls/log.h>
#include <magenta/device/ktrace.h>

#include "apps/tracing/lib/trace/provider.h"
#include "apps/tracing/src/ktrace_provider/importer.h"
#include "apps/tracing/src/ktrace_provider/reader.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"

namespace ktrace_provider {
namespace {

constexpr char kDefaultProviderLabel[] = "ktrace";
constexpr char kKTraceDev[] = "/dev/misc/ktrace";

struct KTraceCategory {
    const char *name;
    uint32_t group;
};

constexpr KTraceCategory kCategories[] = {
    {"kernel", KTRACE_GRP_ALL},
    {"kernel:meta", KTRACE_GRP_META},
    {"kernel:lifecycle", KTRACE_GRP_LIFECYCLE},
    {"kernel:sched", KTRACE_GRP_SCHEDULER},
    {"kernel:tasks", KTRACE_GRP_TASKS},
    {"kernel:ipc", KTRACE_GRP_IPC},
    {"kernel:irq", KTRACE_GRP_IRQ},
    {"kernel:probe", KTRACE_GRP_PROBE},
    {"kernel:arch", KTRACE_GRP_ARCH},
};
}  // namespace

App::App(const ftl::CommandLine& command_line)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
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

uint32_t App::GetGroupMask() {
    uint32_t group_mask = 0;
    for (size_t i = 0; i < arraysize(kCategories); i++) {
      auto &category = kCategories[i];
      if (tracing::writer::IsTracingEnabledForCategory(category.name)) {
          group_mask |= category.group;
      }
    }
    return group_mask;
}

void App::UpdateState(tracing::writer::TraceState state) {
  FTL_VLOG(1) << "UpdateState: state=" << static_cast<int>(state);
  switch (state) {
    case tracing::writer::TraceState::kStarted: {
      uint32_t group_mask = GetGroupMask();
      if (group_mask) {
        RestartTracing(group_mask);
      }
      break;
    } case tracing::writer::TraceState::kStopping:
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

ftl::UniqueFD App::OpenKTrace() {
  int result = open(kKTraceDev, O_WRONLY);
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to open " << kKTraceDev << ": errno=" << errno;
  }
  return ftl::UniqueFD(result);  // take ownership here
}

void App::RestartTracing(uint32_t group_mask) {
  auto fd = OpenKTrace();
  if (!fd.is_valid()) {
      return;
  }

  ioctl_ktrace_stop(fd.get());
  ioctl_ktrace_start(fd.get(), &group_mask);
  trace_running_ = true;
  log_importer_.Start();
}

void App::StopTracing() {
  if (trace_running_) {
    trace_running_ = false;
    auto fd = OpenKTrace();
    if (fd.is_valid()) {
        ioctl_ktrace_stop(fd.get());
    }
    log_importer_.Stop();
  }
}

void App::CollectTraces() {
  auto writer = tracing::writer::TraceWriter::Prepare();
  if (!writer) {
    FTL_LOG(ERROR) << "Failed to prepare writer.";
    return;
  }

  Reader reader;

  Importer importer(writer);
  if (!importer.Import(reader)) {
    FTL_LOG(ERROR) << "Errors encountered while importing ktrace data";
  }
}

}  // namespace ktrace_provider
