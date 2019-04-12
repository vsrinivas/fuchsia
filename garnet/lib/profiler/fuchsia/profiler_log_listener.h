// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PROFILER_FUCHSIA_PROFILER_LOG_LISTENER_H_
#define GARNET_LIB_PROFILER_FUCHSIA_PROFILER_LOG_LISTENER_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/wire_format.h>
#include <zircon/syscalls/log.h>

#include <vector>

#include "lib/fidl/cpp/binding.h"
#include "lib/syslog/cpp/logger.h"

class ProfilerLogListener : public fuchsia::logger::LogListener {
 public:
  ProfilerLogListener(fit::function<void()> all_done);

  void LogMany(::std::vector<fuchsia::logger::LogMessage> Log) override;
  void Log(fuchsia::logger::LogMessage Log) override;
  void Done() override;
  ~ProfilerLogListener() override;

  void CollectLogs(size_t expected_logs);
  bool ConnectToLogger(sys::ComponentContext* component_context, zx_koid_t pid);
  std::string Log() { return log_buffer_.str(); }

 private:
  enum log_entry_kind { RESET, MODULE, MMAP, DSO, SKIP, DONE, ERROR };

  log_entry_kind parse_log_entry(const std::string& log_line);

  fit::function<void()> all_done_;
  ::fidl::Binding<fuchsia::logger::LogListener> binding_;
  fuchsia::logger::LogListenerPtr log_listener_;
  std::stringbuf log_buffer_;
  std::ostream log_os_;
  std::vector<std::vector<std::string>> mmap_entry_;
};

std::string CollectProfilerLog();

#endif  // GARNET_LIB_PROFILER_FUCHSIA_PROFILER_LOG_LISTENER_H_
