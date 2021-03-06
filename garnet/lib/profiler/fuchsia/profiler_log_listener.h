// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PROFILER_FUCHSIA_PROFILER_LOG_LISTENER_H_
#define GARNET_LIB_PROFILER_FUCHSIA_PROFILER_LOG_LISTENER_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/wire_format.h>
#include <zircon/syscalls/log.h>

#include <vector>

#include "lib/fidl/cpp/binding.h"

class ProfilerLogListener : public fuchsia::logger::LogListenerSafe {
 public:
  ProfilerLogListener(fit::function<void()> all_done);

  void LogMany(::std::vector<fuchsia::logger::LogMessage> Log, LogManyCallback received) override;
  void Log(fuchsia::logger::LogMessage Log, LogCallback received) override;
  void Done() override;
  ~ProfilerLogListener() override;

  void CollectLogs(size_t expected_logs);
  bool ConnectToLogger(sys::ComponentContext* component_context, zx_koid_t pid);
  std::string Log() { return log_buffer_.str(); }

 private:
  enum class LogEntryKind { Module, Mmap };
  struct LogEntry {
    LogEntryKind kind;

    uintptr_t module_id;
    std::string module_name;
    uintptr_t module_base_address;

    uintptr_t mmap_address;
    uintptr_t mmap_size;
    uintptr_t mmap_module_id;
    std::string mmap_access;
    uintptr_t mmap_offset;
  };

  void ParseLogEntry(const std::string& log_line);
  void FlushLogEntryQueue();

  fit::function<void()> all_done_;
  ::fidl::Binding<fuchsia::logger::LogListenerSafe> binding_;
  fuchsia::logger::LogListenerSafePtr log_listener_;
  std::stringbuf log_buffer_;
  std::ostream log_os_;
  std::vector<LogEntry> log_entry_queue_;
};

std::string CollectProfilerLog();

#endif  // GARNET_LIB_PROFILER_FUCHSIA_PROFILER_LOG_LISTENER_H_
