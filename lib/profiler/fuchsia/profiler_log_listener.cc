// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/profiler/fuchsia/profiler_log_listener.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <zircon/sanitizer.h>

ProfilerLogListener::ProfilerLogListener() : binding_(this) {
  binding_.Bind(log_listener_.NewRequest());
}

ProfilerLogListener::~ProfilerLogListener() {}

void ProfilerLogListener::LogMany(
    ::fidl::VectorPtr<fuchsia::logger::LogMessage> logs) {
  std::move(logs->begin(), logs->end(), std::back_inserter(log_messages_));
}

void ProfilerLogListener::Log(fuchsia::logger::LogMessage log) {
  log_messages_.push_back(std::move(log));
}

void ProfilerLogListener::Done() {}

bool ProfilerLogListener::ConnectToLogger(
    component::StartupContext* startup_context, zx_koid_t pid) {
  if (!log_listener_) {
    return false;
  }
  auto log_service =
      startup_context->ConnectToEnvironmentService<fuchsia::logger::Log>();
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
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

static zx_koid_t GetCurrentProcessKoid() {
  auto koid = GetKoid(zx::process::self()->get());
  ZX_DEBUG_ASSERT(koid != ZX_KOID_INVALID);
  return koid;
}

static bool RunGivenLoopWithTimeout(async::Loop* loop, zx::duration timeout) {
  // This cannot be a local variable because the delayed task below can execute
  // after this function returns.
  auto canceled = std::make_shared<bool>(false);
  bool timed_out = false;
  async::PostDelayedTask(loop->dispatcher(),
                         [loop, canceled, &timed_out] {
                           if (*canceled) {
                             return;
                           }
                           timed_out = true;
                           loop->Quit();
                         },
                         timeout);
  loop->Run();
  loop->ResetQuit();
  // Another task can call Quit() on the message loop, which exits the
  // message loop before the delayed task executes, in which case |timed_out| is
  // still false here because the delayed task hasn't run yet.
  // Since the message loop isn't destroyed then (as it usually would after
  // Quit()), and presumably can be reused after this function returns we
  // still need to prevent the delayed task to quit it again at some later time
  // using the canceled pointer.
  if (!timed_out) {
    *canceled = true;
  }
  return timed_out;
}

static bool RunGivenLoopWithTimeoutOrUntil(async::Loop* loop,
                                           fit::function<bool()> condition,
                                           zx::duration timeout = zx::sec(1),
                                           zx::duration step = zx::msec(10)) {
  const zx::time deadline = (timeout == zx::sec(0))
                                ? zx::time::infinite()
                                : zx::deadline_after(timeout);
  while (zx::clock::get_monotonic() < deadline) {
    if (condition()) {
      return true;
    }
    RunGivenLoopWithTimeout(loop, step);
  }
  return condition();
}

std::string reformat_log(const std::vector<std::string>& log_strings) {
  std::vector<std::vector<std::string>> mmap_entry;

  std::stringbuf buffer;
  std::ostream os(&buffer);

  for (const std::string& log_line : log_strings) {
    if (log_line.compare(0, 3, "{{{") == 0 &&
        log_line.compare(log_line.size() - 3, 3, "}}}") == 0) {
      const std::string s = log_line.substr(3, log_line.size() - 6);

      std::stringstream input_stringstream(s);
      std::vector<std::string> tokens;
      std::stringstream ss(s);
      std::string item;
      while (getline(ss, item, ':')) {
        tokens.push_back(item);
      }
      if (tokens[0].compare("module") == 0 && tokens.size() == 5) {
        if (mmap_entry.size() != 0) {
          return "module out of step";
        }
        mmap_entry.push_back(tokens);
      } else if (tokens[0].compare("mmap") == 0 && tokens.size() == 7) {
        mmap_entry.push_back(tokens);
      } else if (tokens[0].compare("reset") == 0) {
        // Skip
      } else {
        return "unexpected token";
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
      if (tokens[0].compare(0, 3, "id=") == 0 &&
          tokens[1].compare(0, 5, "base=") == 0 &&
          tokens[2].compare(0, 5, "name=") == 0) {
        id = tokens[0].substr(3, s.size() - 3);
        base = tokens[1].substr(5, s.size() - 5);
        name = tokens[2].substr(5, s.size() - 5);
        if (name.compare("<vDSO>") == 0) {
          name = "libzircon.so";
        }
      } else {
        return "unexpected DSO structure";
      }

      for (unsigned int i = 1; i < mmap_entry.size(); i++) {
        std::vector<std::string> mmap = mmap_entry[i];

        uintptr_t address = std::stoul(mmap[1], nullptr, 16);
        uintptr_t size = std::stoul(mmap[2], nullptr, 16);
        uintptr_t offset = std::stoul(mmap[6], nullptr, 16);
        uintptr_t module = std::stoul(mmap[4], nullptr, 16);
        std::string access =
            mmap[5].compare("r") == 0
                ? "r--p"
                : mmap[5].compare("rw") == 0
                      ? "rw-p"
                      : mmap[5].compare("rx") == 0
                            ? "r-xp"
                            : mmap[5].compare("rwx") == 0
                                  ? "rwxp"
                                  :  // Can you actually do this?
                                  "---p";

        os << std::hex << std::setfill('0') << std::setw(12) << address << '-'
           << std::setw(12) << address + size << " " << access << " "
           << std::setw(8) << std::setfill('0') << offset << " "
           << "00:00" << std::setw(4) << std::setfill(' ') << std::dec << module
           << " " << name << '\n';
      }

      mmap_entry.clear();
    } else {
      // Generic log stuff, just ignore for now
    }
  }

  return buffer.str();
}

std::string CollectProfilerLog() {
  static const char termination_message[] = "The End";
  __sanitizer_log_write(termination_message, sizeof(termination_message) - 1);

  async::Loop loop_(&kAsyncLoopConfigAttachToThread);

  ProfilerLogListener log_listener;

  auto pid = GetCurrentProcessKoid();

  auto startup_context =
      component::StartupContext::CreateFromStartupInfoNotChecked();
  log_listener.ConnectToLogger(startup_context.get(), pid);
  auto& logs = log_listener.GetLogs();

  RunGivenLoopWithTimeoutOrUntil(&loop_, [&logs] { return logs.size() >= 1u; },
                                 zx::sec(5));

  std::vector<std::string> log_strings;

  for (unsigned int i = 0; i < logs.size(); i++) {
    const std::string msg = logs[i].msg.get();
    if (msg.compare(termination_message) == 0)
      break;
    log_strings.push_back(msg);
  }

  return reformat_log(log_strings);
}
