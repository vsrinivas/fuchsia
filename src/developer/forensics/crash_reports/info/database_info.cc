// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/info/database_info.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace crash_reports {

DatabaseInfo::DatabaseInfo(std::shared_ptr<InfoContext> context) : context_(context) {
  FX_CHECK(context_);
}

void DatabaseInfo::CrashpadError(const cobalt::CrashpadFunctionError function) {
  context_->Cobalt().LogOccurrence(function);
}

void DatabaseInfo::LogMaxCrashpadDatabaseSize(const StorageSize max_crashpad_database_size) {
  context_->InspectManager().ExposeDatabase(max_crashpad_database_size.ToKilobytes());
}

void DatabaseInfo::LogGarbageCollection(const uint64_t num_cleaned, const uint64_t num_pruned) {
  context_->InspectManager().IncreaseReportsCleanedBy(num_cleaned);
  context_->InspectManager().IncreaseReportsPrunedBy(num_pruned);
}

}  // namespace crash_reports
}  // namespace forensics
