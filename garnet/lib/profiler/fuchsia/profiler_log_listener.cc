// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/profiler/fuchsia/profiler_log_listener.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/sanitizer.h>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

ProfilerLogListener::ProfilerLogListener(fit::function<void()> all_done)
    : all_done_(std::move(all_done)), binding_(this), log_os_(&log_buffer_) {
  binding_.Bind(log_listener_.NewRequest());
}

ProfilerLogListener::~ProfilerLogListener() {}

void ProfilerLogListener::LogMany(::std::vector<fuchsia::logger::LogMessage> logs,
                                  LogManyCallback received) {
  for (auto& log_entry : logs) {
    ParseLogEntry(log_entry.msg);
  }
  received();
}

static const char termination_message[] = "{{{done}}}";

void ProfilerLogListener::ParseLogEntry(const std::string& log_line) {
  if (log_line.compare(0, 3, "{{{") != 0 || log_line.compare(log_line.size() - 3, 3, "}}}") != 0) {
    return;
  }

  const std::string s = log_line.substr(3, log_line.size() - 6);

  std::stringstream input_stringstream(s);
  std::vector<std::string> tokens;
  std::stringstream ss(s);
  std::string item;
  while (getline(ss, item, ':')) {
    tokens.push_back(item);
  }
  if (tokens[0] == "module" && tokens.size() == 5) {
    FlushLogEntryQueue();
    log_entry_queue_.push_back(LogEntry{
        .kind = LogEntryKind::Module,
        .module_id = std::stoul(tokens[1], nullptr, 16),
        .module_name = tokens[2],
        .module_base_address = std::stoul(tokens[4], nullptr, 16),
    });
  } else if (tokens[0] == "mmap" && tokens.size() == 7) {
    log_entry_queue_.push_back(LogEntry{
        .kind = LogEntryKind::Mmap,
        .mmap_address = std::stoul(tokens[1], nullptr, 16),
        .mmap_size = std::stoul(tokens[2], nullptr, 16),
        .mmap_module_id = std::stoul(tokens[4], nullptr, 16),
        .mmap_access = tokens[5],
        .mmap_offset = std::stoul(tokens[6], nullptr, 16),
    });
  } else if (tokens[0] == "reset") {
    log_entry_queue_.clear();
  } else if (tokens[0] == "done") {
    FlushLogEntryQueue();
    all_done_();
  } else {
    fprintf(stderr, "WARNING: ProfilerLogListener: unexpected token: %s\n", tokens[0].c_str());
  }
}

void ProfilerLogListener::FlushLogEntryQueue() {
  if (log_entry_queue_.empty()) {
    return;
  }

  // First entry must be a module.
  if (log_entry_queue_[0].kind != LogEntryKind::Module) {
    fprintf(stderr,
            "WARNING: ProfilerLogListener: Skipping log entry with kind=%d (expected module)\n",
            static_cast<int>(log_entry_queue_[0].kind));
    log_entry_queue_.clear();
    return;
  }

  auto module_id = log_entry_queue_[0].module_id;
  auto module_name = log_entry_queue_[0].module_name;
  if (module_name == "<vDSO>") {
    module_name = "libzircon.so";
  }

  // Remaining entries must be mmaps.
  for (auto entry = log_entry_queue_.begin() + 1; entry != log_entry_queue_.end(); entry++) {
    if (entry->kind != LogEntryKind::Mmap) {
      fprintf(stderr,
              "WARNING: ProfilerLogListener: Skipping log entry with kind=%d (expected mmap)\n",
              static_cast<int>(log_entry_queue_[0].kind));
      continue;
    }
    if (entry->mmap_module_id != module_id) {
      fprintf(stderr, "ProfilerLogListener: Found module id %lu, expected %lu\n",
              entry->mmap_module_id, module_id);
    }

    auto access = entry->mmap_access.compare("r") == 0     ? "r--p"
                  : entry->mmap_access.compare("rw") == 0  ? "rw-p"
                  : entry->mmap_access.compare("rx") == 0  ? "r-xp"
                  : entry->mmap_access.compare("rwx") == 0 ? "rwxp"  // Can you actually do this?
                                                           : "---p";

    log_os_ << std::hex << std::setfill('0') << std::setw(12) << entry->mmap_address << '-'
            << std::setw(12) << (entry->mmap_address + entry->mmap_size) << " " << access << " "
            << std::setw(8) << std::setfill('0') << entry->mmap_offset << " "
            << "00:00" << std::setw(4) << std::setfill(' ') << std::dec << entry->mmap_module_id
            << " " << module_name << '\n';
  }

  log_entry_queue_.clear();
}

void ProfilerLogListener::Log(fuchsia::logger::LogMessage log, LogCallback received) {
  ParseLogEntry(std::move(log.msg));
  received();
}

void ProfilerLogListener::Done() {}

bool ProfilerLogListener::ConnectToLogger(sys::ComponentContext* component_context, zx_koid_t pid) {
  if (!log_listener_) {
    return false;
  }

  auto log_service = component_context->svc()->Connect<fuchsia::logger::Log>();
  auto options = std::make_unique<fuchsia::logger::LogFilterOptions>();
  options->filter_by_pid = true;
  options->pid = pid;
  // make tags non-null.
  options->tags.resize(0);
  log_service->ListenSafe(std::move(log_listener_), std::move(options));
  return true;
}

static zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

static zx_koid_t GetCurrentProcessKoid() {
  auto koid = GetKoid(zx::process::self()->get());
  ZX_DEBUG_ASSERT(koid != ZX_KOID_INVALID);
  return koid;
}

std::string CollectProfilerLog() {
  async::Loop loop_(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop_.StartThread();

  std::unique_ptr<ProfilerLogListener> log_listener;
  async::PostTask(loop_.dispatcher(), [&log_listener, &loop_] {
    async_set_default_dispatcher(loop_.dispatcher());

    log_listener = std::make_unique<ProfilerLogListener>([&loop_] {
      // Done parsing the log
      loop_.Quit();
    });

    auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
    log_listener->ConnectToLogger(component_context.get(), GetCurrentProcessKoid());

    // Trigger mmap log
    __sanitizer_log_write(termination_message, sizeof(termination_message) - 1);
  });
  loop_.Run();
  loop_.JoinThreads();

  return log_listener->Log();
}
