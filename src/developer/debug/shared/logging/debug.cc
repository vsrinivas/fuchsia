// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/debug.h"

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <chrono>
#include <mutex>
#include <set>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug {

namespace {

// This marks the moment our process starts.
std::chrono::steady_clock::time_point kStartTime = std::chrono::steady_clock::now();

std::set<LogCategory>& GetLogCategories() {
  static std::set<LogCategory> categories = {LogCategory::kAll};
  return categories;
}

bool IsLogCategoryActive(LogCategory category) {
  if (!IsDebugLoggingActive())
    return false;

  if (category == LogCategory::kAll)
    return true;

  const auto& active_categories = GetLogCategories();
  if (active_categories.count(LogCategory::kAll) > 0)
    return true;

  return active_categories.count(category) > 0;
}

const char* LogCategoryToString(LogCategory category) {
  switch (category) {
    case LogCategory::kAgent:
      return "Agent";
    case LogCategory::kArchArm64:
      return "arm64";
    case LogCategory::kArchx64:
      return "x64";
    case LogCategory::kBreakpoint:
      return "Breakpoint";
    case LogCategory::kJob:
      return "Job";
    case LogCategory::kMessageLoop:
      return "Loop";
    case LogCategory::kProcess:
      return "Process";
    case LogCategory::kRemoteAPI:
      return "DebugAPI";
    case LogCategory::kSession:
      return "Session";
    case LogCategory::kSetting:
      return "Setting";
    case LogCategory::kTest:
      return "Test";
    case LogCategory::kTiming:
      return "Timing";
    case LogCategory::kThread:
      return "Thread";
    case LogCategory::kWatchpoint:
      return "Watchpoint";
    case LogCategory::kWorkerPool:
      return "WorkerPool";
    case LogCategory::kDebugAdapter:
      return "DebugAdapter";
    case LogCategory::kAll:
      return "All";
    case LogCategory::kNone:
      return "<none>";
  }

  FX_NOTREACHED();
  return "<unknown>";
}

// Returns how many seconds have passed since the program started.
double SecondsSinceStart() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - kStartTime).count();
}

}  // namespace

bool IsDebugLoggingActive() { return syslog::ShouldCreateLogMessage(syslog::LOG_DEBUG); }

void SetDebugLogging(bool activate) {
  syslog::LogSettings settings;
  if (activate) {
    settings.min_log_level = syslog::LOG_DEBUG;
  }
  syslog::SetLogSettings(settings);
}

void SetLogCategories(std::initializer_list<LogCategory> categories) {
  auto& active_categories = GetLogCategories();
  active_categories.clear();
  for (LogCategory category : categories) {
    active_categories.insert(category);
  }
}

// Log Tree ----------------------------------------------------------------------------------------
//
// To facilitate logging, messages are appended to a tree and actually filled on the logging
// instance destructor. This permits to correctly track under which block each message was logged
// and give better context on output.
//
// The pop gets the message information because the logging logic using ostreams. This means that
// the actual message is constructored *after* the logging object has been constructed, which is
// the obvious message to push an entry. Plus, this logic permits to do messages that have
// information that it's only present after the block is done (like timing information).
//
// IMPORTANT: Because this delays when the log output, any abnormal termination (eg. crash) might
//            eat the latest batch of logs that is currently on the stack. Possible workaround is
//            having an exception handler in fuchsia and/or a signal handler in linux to flush the
//            rest of the output in the case of a crash.

namespace {

// Logs are aranged in a tree that permits going back to the parent.
struct LogEntry {
  LogCategory category = LogCategory::kNone;
  FileLineFunction location;
  std::string msg;
  double start_time = 0;
  double end_time = 0;

  std::vector<std::unique_ptr<LogEntry>> children;
  LogEntry* parent = nullptr;

  // If a statement is valid, it means that it has not been flushed out of the stack, thus the
  // statement is still valid in memory. We can use this backpointer to flush the statement.
  DebugLogStatement* statement = nullptr;
};

LogEntry gRootSlot;
LogEntry* gCurrentSlot = nullptr;

std::mutex gLogMutex;

// Output is dd:hh:mm:ss.<ms>. dd is only showen when non-zero.
std::string SecondsToTimeString(double ds) {
  int total_secs = static_cast<int>(ds);
  int s = total_secs % 60;

  int total_min = total_secs / 60;
  int m = total_min % 60;

  int total_hours = total_min / 60;
  int h = total_hours % 24;

  int d = total_hours / 24;

  int ms = static_cast<int>((ds - total_secs) * 1000);

  // We don't want to add days if it's 0, as it adds noise and it will be rare to have them.
  if (d == 0) {
    return fxl::StringPrintf("%02d:%02d:%02d.%03d", h, m, s, ms);
  } else {
    return fxl::StringPrintf("%02d:%02d:%02d:%02d.%03d", d, h, m, s, ms);
  }
}

// Output is <sec>.<ms> (eg. 32.453).
std::string DurationToString(double start, double end) {
  double diff = end - start;
  int s = static_cast<int>(diff);
  int ms = static_cast<int>((diff - s) * 1000000);

  return fxl::StringPrintf("%03d.%06ds", s, ms);
}

// Format is (depending on whether |entry.location| is valid or not:
// [<time>][<category>]<indent><log msg>    (location invalid).
// [<time>][<category>]<indent>[<function>][<file:line>] <log msg>
std::string LogEntryToStr(const LogEntry& entry, int indent) {
  auto start_time_str = SecondsToTimeString(entry.start_time);
  const char* cat_str = LogCategoryToString(entry.category);

  // No location is only timing information.
  if (!entry.location.is_valid()) {
    return fxl::StringPrintf("[%s][%10s]%*s%s", start_time_str.c_str(), cat_str, indent, "",
                             entry.msg.c_str());
  }

  auto duration_str = DurationToString(entry.start_time, entry.end_time);
  auto file = files::GetBaseName(entry.location.file());
  int line = entry.location.line();
  const char* function = entry.location.function();

  return fxl::StringPrintf("[%s][%s][%10s]%*s[%s:%d][%s] %s", start_time_str.c_str(),
                           duration_str.c_str(), cat_str, indent, "", file.c_str(), line, function,
                           entry.msg.c_str());
}

// Goes over the logging tree recursively and correctly indents the log messages into |logs|.
void UnwindLogTree(const LogEntry& entry, std::vector<std::string>* logs, int indent = 0) {
  logs->emplace_back(LogEntryToStr(entry, indent));

  for (auto& child : entry.children) {
    UnwindLogTree(*child, logs, indent + 2);
  }
}

void PushLogEntry(DebugLogStatement* statement) {
  std::lock_guard<std::mutex> lock(gLogMutex);
  // No slot means that it's a new message tree. Use the root.
  if (!gCurrentSlot) {
    gRootSlot = {};
    gCurrentSlot = &gRootSlot;
  } else {
    // Push a child.
    gCurrentSlot->children.push_back(std::make_unique<LogEntry>());
    auto& child = gCurrentSlot->children.back();
    child->parent = gCurrentSlot;
    gCurrentSlot = child.get();
  }

  gCurrentSlot->statement = statement;
}

void PopLogEntry(LogCategory category, const FileLineFunction& location, const std::string& msg,
                 double start_time, double end_time) {
  std::vector<std::string> logs;
  {
    std::lock_guard<std::mutex> lock(gLogMutex);

    // Set the message that's going away to the child.
    gCurrentSlot->category = category;
    gCurrentSlot->location = location;
    gCurrentSlot->msg = msg;
    gCurrentSlot->start_time = start_time;
    gCurrentSlot->end_time = end_time;
    gCurrentSlot->statement = nullptr;

    // While there is still a parent, we're not at the root.
    if (gCurrentSlot->parent) {
      gCurrentSlot = gCurrentSlot->parent;
      return;
    }

    // We tried to pop the root, that means this logging tree is done and we're going to output it
    // and reset everything.
    UnwindLogTree(*gCurrentSlot, &logs);
    gCurrentSlot = nullptr;
  }

#if defined(__Fuchsia__)
  for (auto& log : logs) {
    FX_LOGS(DEBUG) << log;
  }
#else
  for (auto& log : logs) {
    fprintf(stderr, "\r%s\r\n", log.c_str());
  }
  fflush(stderr);
#endif
}

}  // namespace

DebugLogStatement::DebugLogStatement(FileLineFunction origin, LogCategory category)
    : origin_(std::move(origin)), category_(category) {
  if (!IsLogCategoryActive(category_))
    return;

  start_time_ = SecondsSinceStart();
  should_log_ = true;
  PushLogEntry(this);
}

DebugLogStatement::~DebugLogStatement() {
  if (!should_log_)
    return;

  PopLogEntry(category_, origin_, stream_.str(), start_time_, SecondsSinceStart());
}

}  // namespace debug
