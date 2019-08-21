// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/debug.h"

#include <mutex>
#include <set>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/time/time_point.h"

namespace debug_ipc {

namespace {

bool kDebugMode = false;

// This marks the moment SetDebugMode was called.
fxl::TimePoint kStartTime = fxl::TimePoint::Now();

std::set<LogCategory>& GetLogCategories() {
  static std::set<LogCategory> categories = {LogCategory::kAll};
  return categories;
}

}  // namespace

bool IsDebugModeActive() { return kDebugMode; }

void SetDebugMode(bool activate) { kDebugMode = activate; }

double SecondsSinceStart() { return (fxl::TimePoint::Now() - kStartTime).ToSecondsF(); }

const std::set<LogCategory>& GetActiveLogCategories() { return GetLogCategories(); }

void SetLogCategories(std::initializer_list<LogCategory> categories) {
  auto& active_categories = GetLogCategories();
  active_categories.clear();
  for (LogCategory category : categories) {
    active_categories.insert(category);
  }
}

bool IsLogCategoryActive(LogCategory category) {
  if (!IsDebugModeActive())
    return false;

  if (category == LogCategory::kAll)
    return true;

  const auto& active_categories = GetLogCategories();
  if (active_categories.count(LogCategory::kAll) > 0)
    return true;

  return active_categories.count(category) > 0;
}

// Log Tree ----------------------------------------------------------------------------------------

// Log tree works by pusing a log statement when the logging is done but waiting until that block
// is done to pop it out of the stack. By waiting until the whole stack is popped, we can flush the
// logs with a correct log context.
//
// This has several drawbacks though, such as additional context is needed to reconstruct logs in
// case of a crash (backpointer to the log statement object) and that depending on the stack, log
// statements might not appear for a while.

namespace {

// Logs are aranged in a tree that permits going back to the parent.
struct LogEntry {
  LogCategory category;
  FileLineFunction origin;
  std::string msg;
  double time = 0;

  std::vector<std::unique_ptr<LogEntry>> children;
  LogEntry* parent = nullptr;

  // If a statement is valid, it means that it has not been flushed out of the stack, thus the
  // statement is still valid in memory. We can use this backpointer to flush the statement.
  LogStatement* statement = nullptr;
};

LogEntry gRootSlot;
LogEntry* gCurrentSlot = nullptr;

std::mutex gLogMutex;

// Goes over the logging tree recursively and correctly indents the log messages into |logs|.
void UnwindLogTree(const LogEntry& slot, std::vector<std::string>* logs, int indent = 0) {
  logs->emplace_back(LogWithPreamble(slot.category, slot.origin, slot.msg, slot.time, indent));
  for (auto& child : slot.children) {
    UnwindLogTree(*child, logs, indent + 1);
  }
}

void FillInLogEntry(LogEntry* entry) {
  if (entry->origin.file().empty()) {
    if (!entry->statement)
      return;

    // Refill the slot with the log statement.
    entry->category = entry->statement->category();
    entry->origin = entry->statement->origin();
    entry->msg = entry->statement->GetMsg();
    entry->time = entry->statement->time();
    entry->statement = nullptr;
  }
}

void TraverseLogTree(LogEntry* slot, std::vector<std::string>* logs, int indent = 0) {
  FillInLogEntry(slot);

  logs->emplace_back(LogWithPreamble(slot->category, slot->origin, slot->msg, slot->time, indent));
  for (auto& child_slot : slot->children) {
    TraverseLogTree(child_slot.get(), logs, indent + 1);
  }
}

// Output is dd:hh:mm:ss.<ms>. dd is only showen when non-zero.
std::string SecondsToString(double ds) {
  int total_secs = (int)ds;
  int s = total_secs % 60;

  int total_min = total_secs / 60;
  int m = total_min % 60;

  int total_hours = total_min / 60;
  int h = total_hours % 24;

  int d = total_hours / 24;

  int ms = (int)((ds - total_secs) * 1000);

  // We don't want to add days if it's 0, as it adds noise and it will be rare to have them.
  if (d == 0) {
    return fxl::StringPrintf("%02d:%02d:%02d.%03d", h, m, s, ms);
  } else {
    return fxl::StringPrintf("%02d:%02d:%02d:%02d.%03d", d, h, m, s, ms);
  }
}

}  // namespace

void PushLogEntry(LogStatement* statement) {
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

void PopLogEntry(LogCategory category, const FileLineFunction& origin, const std::string& msg,
                 double time) {
  std::vector<std::string> logs;
  {
    std::lock_guard<std::mutex> lock(gLogMutex);

    // Set the message that's going away to the child.
    gCurrentSlot->category = category;
    gCurrentSlot->origin = origin;
    gCurrentSlot->msg = msg;
    gCurrentSlot->time = time;
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

  for (auto& log : logs) {
    fprintf(stderr, "\r%s\r\n", log.c_str());
  }
  fflush(stderr);
}

void FlushLogEntries() {
  std::vector<std::string> logs;

  {
    if (!gLogMutex.try_lock())
      return;
    TraverseLogTree(&gRootSlot, &logs);
    gLogMutex.unlock();
  }

  for (auto& log : logs) {
    fprintf(stderr, "\rLOG: %s\r\n", log.c_str());
  }
  fflush(stderr);
}

// The format is:
// [<time>][<category>][<function>][<file:line>] <log msg>
std::string LogWithPreamble(LogCategory category, const FileLineFunction& origin,
                            const std::string& msg, double time, int indent) {
  constexpr int kIndentationSize = 2;
  if (origin.file().empty()) {
    return fxl::StringPrintf("[%s][%10s]%*s<empty>", SecondsToString(time).c_str(), "<unknown>",
                             indent * kIndentationSize, " ");
  }

  auto basename = files::GetBaseName(origin.file());
  std::string log;
  if (indent > 0) {
    log =
        fxl::StringPrintf("[%s][%10s]%*s[%s][%s:%d] %s", SecondsToString(time).c_str(),
                          LogCategoryToString(category), indent * kIndentationSize, " ",
                          origin.function().c_str(), basename.c_str(), origin.line(), msg.c_str());
  } else {
    log = fxl::StringPrintf("[%s][%10s][%s][%s:%d] %s", SecondsToString(time).c_str(),
                            LogCategoryToString(category), origin.function().c_str(),
                            basename.c_str(), origin.line(), msg.c_str());
  }

  return log;
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
    case LogCategory::kAll:
      return "All";
  }

  FXL_NOTREACHED();
  return nullptr;
}

}  // namespace debug_ipc
