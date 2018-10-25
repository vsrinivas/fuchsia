// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PROFILER_FUCHSIA_PROFILER_LOG_LISTENER_H_
#define GARNET_LIB_PROFILER_FUCHSIA_PROFILER_LOG_LISTENER_H_

#include <vector>

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/syslog/wire_format.h>
#include <zircon/syscalls/log.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/syslog/cpp/logger.h"

class ProfilerLogListener : public fuchsia::logger::LogListener {
 public:
  ProfilerLogListener();

  void LogMany(::fidl::VectorPtr<fuchsia::logger::LogMessage> Log) override;
  void Log(fuchsia::logger::LogMessage Log) override;
  void Done() override;
  ~ProfilerLogListener() override;

  const std::vector<fuchsia::logger::LogMessage>& GetLogs() {
    return log_messages_;
  }
  void CollectLogs(size_t expected_logs);
  bool ConnectToLogger(component::StartupContext* startup_context,
                       zx_koid_t pid);

 private:
  ::fidl::Binding<fuchsia::logger::LogListener> binding_;
  fuchsia::logger::LogListenerPtr log_listener_;
  std::vector<fuchsia::logger::LogMessage> log_messages_;
};

std::string CollectProfilerLog();

#endif  // GARNET_LIB_PROFILER_FUCHSIA_PROFILER_LOG_LISTENER_H_
