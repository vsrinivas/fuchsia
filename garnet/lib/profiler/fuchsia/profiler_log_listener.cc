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

void ProfilerLogListener::LogMany(::std::vector<fuchsia::logger::LogMessage> logs) {
  for (auto&& log_entry : logs) {
    log_entry_kind token = parse_log_entry(std::move(log_entry.msg));
    if (token == DONE) {
      all_done_();
    }
  }
}

static const char termination_message[] = "{{{done}}}";

ProfilerLogListener::log_entry_kind ProfilerLogListener::parse_log_entry(
    const std::string& log_line) {
  if (log_line.compare(0, 3, "{{{") == 0 && log_line.compare(log_line.size() - 3, 3, "}}}") == 0) {
    const std::string s = log_line.substr(3, log_line.size() - 6);

    std::stringstream input_stringstream(s);
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string item;
    while (getline(ss, item, ':')) {
      tokens.push_back(item);
    }
    if (tokens[0].compare("module") == 0 && tokens.size() == 5) {
      if (mmap_entry_.size() != 0) {
        printf("module out of step\n");
        return ERROR;  // "module out of step";
      }
      mmap_entry_.push_back(tokens);
      return MODULE;
    } else if (tokens[0].compare("mmap") == 0 && tokens.size() == 7) {
      mmap_entry_.push_back(tokens);
      return MMAP;
    } else if (tokens[0].compare("reset") == 0) {
      mmap_entry_.clear();
      return RESET;
    } else if (tokens[0].compare("done") == 0) {
      mmap_entry_.clear();
      return DONE;
    } else {
      printf("unexpected token: %s\n", tokens[0].c_str());
      return ERROR;  // "unexpected token";
    }
  } else if (log_line.compare(0, 5, "dso: ") == 0) {
    std::string s = log_line.substr(5, log_line.size() - 5);

    std::stringstream input_stringstream(s);
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string item;
    while (getline(ss, item, ' ')) {
      tokens.push_back(item);
    }
    std::string id, base, name;
    if (tokens[0].compare(0, 3, "id=") == 0 && tokens[1].compare(0, 5, "base=") == 0 &&
        tokens[2].compare(0, 5, "name=") == 0) {
      id = tokens[0].substr(3, s.size() - 3);
      base = tokens[1].substr(5, s.size() - 5);
      name = tokens[2].substr(5, s.size() - 5);
      if (name.compare("<vDSO>") == 0) {
        name = "libzircon.so";
      }
    } else {
      printf("unexpected DSO structure\n");
      return ERROR;  // "unexpected DSO structure";
    }

    for (unsigned int i = 1; i < mmap_entry_.size(); i++) {
      std::vector<std::string> mmap = mmap_entry_[i];

      uintptr_t address = std::stoul(mmap[1], nullptr, 16);
      uintptr_t size = std::stoul(mmap[2], nullptr, 16);
      uintptr_t offset = std::stoul(mmap[6], nullptr, 16);
      uintptr_t module = std::stoul(mmap[4], nullptr, 16);
      std::string access = mmap[5].compare("r") == 0
                               ? "r--p"
                               : mmap[5].compare("rw") == 0
                                     ? "rw-p"
                                     : mmap[5].compare("rx") == 0
                                           ? "r-xp"
                                           : mmap[5].compare("rwx") == 0
                                                 ? "rwxp"  // Can you actually do this?
                                                 : "---p";

      log_os_ << std::hex << std::setfill('0') << std::setw(12) << address << '-' << std::setw(12)
              << address + size << " " << access << " " << std::setw(8) << std::setfill('0')
              << offset << " "
              << "00:00" << std::setw(4) << std::setfill(' ') << std::dec << module << " " << name
              << '\n';
    }

    mmap_entry_.clear();

    return DSO;
  } else {
    return SKIP;
  }
}

void ProfilerLogListener::Log(fuchsia::logger::LogMessage log) {
  log_entry_kind token = parse_log_entry(std::move(log.msg));
  if (token == DONE) {
    all_done_();
  }
}

void ProfilerLogListener::Done() {}

bool ProfilerLogListener::ConnectToLogger(sys::ComponentContext* component_context, zx_koid_t pid) {
  if (!log_listener_) {
    return false;
  }
  auto log_service = component_context->svc()->Connect<fuchsia::logger::Log>();
  auto options = fuchsia::logger::LogFilterOptions::New();
  options->filter_by_pid = true;
  options->pid = pid;
  // make tags non-null.
  options->tags.resize(0);
  log_service->Listen(std::move(log_listener_), std::move(options));
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

    auto component_context = sys::ComponentContext::Create();
    log_listener->ConnectToLogger(component_context.get(), GetCurrentProcessKoid());

    // Trigger mmap log
    __sanitizer_log_write(termination_message, sizeof(termination_message) - 1);
  });
  loop_.Run();
  loop_.JoinThreads();

  return log_listener->Log();
}
