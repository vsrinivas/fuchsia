// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/debug.h"

#include <set>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/time/time_point.h"
#include "src/lib/files/path.h"

namespace debug_ipc {

namespace {

bool kDebugMode = false;

// This marks the moment SetDebugMode was called.
fxl::TimePoint kStartTime = fxl::TimePoint::Now();

std::set<LogCategory> active_categories;

}  // namespace

bool IsDebugModeActive() { return kDebugMode; }

void SetDebugMode(bool activate) { kDebugMode = activate; }

double SecondsSinceStart() {
  return (fxl::TimePoint::Now() - kStartTime).ToSecondsF();
}

const std::set<LogCategory>& GetActiveLogCategories() {
  return active_categories;
}

void SetLogCategories(std::initializer_list<LogCategory> categories) {
  for (LogCategory category : categories) {
    active_categories.insert(category);
  }
}

bool IsLogCategoryActive(LogCategory category) {
  if (category == LogCategory::kAll)
    return true;

  if (active_categories.count(LogCategory::kAll) > 0)
    return true;

  return active_categories.count(category) > 0;
}

std::string LogPreamble(LogCategory category, const FileLineFunction& origin) {
  auto basename = files::GetBaseName(origin.file());
  return fxl::StringPrintf("[%.3fs][%8s][%s:%d][%s]", SecondsSinceStart(),
                           LogCategoryToString(category), basename.c_str(),
                           origin.line(), origin.function().c_str());
}

const char* LogCategoryToString(LogCategory category) {
  switch (category) {
    case LogCategory::kJob: return "Job";
    case LogCategory::kMessageLoop: return "Loop";
    case LogCategory::kProcess: return "Process";
    case LogCategory::kRemoteAPI: return "DebugAPI";
    case LogCategory::kTiming: return "Timing";
    case LogCategory::kAll: return "All";
  }

  FXL_NOTREACHED();
  return nullptr;
}

}  // namespace debug_ipc
