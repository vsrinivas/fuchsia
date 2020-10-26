// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/log_tags.h"

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace crash_reports {

void LogTags::Register(const ReportId report_id, const std::vector<std::string>& tags) {
  tags_[report_id] = fxl::StringPrintf("%s, %zu", fxl::JoinStrings(tags, ", ").c_str(), report_id);
}

void LogTags::Unregister(const ReportId report_id) { tags_.erase(report_id); }

const char* LogTags::Get(const ReportId report_id) {
  return (tags_.find(report_id) != tags_.end()) ? tags_[report_id].c_str() : nullptr;
}

}  // namespace crash_reports
}  // namespace forensics
