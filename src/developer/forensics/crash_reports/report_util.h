// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORT_UTIL_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORT_UTIL_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/result.h>

#include <map>
#include <optional>
#include <string>

#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/crash_reports/report.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace crash_reports {

// Shorten |program_name| into a shortname by removing the "fuchsia-pkg://" prefix if present and
// replacing all '/' with ':'.
//
// For example `fuchsia-pkg://fuchsia.com/crash-reports#meta/crash_reports.cmx` becomes
// `fuchsia.com:crash-reports#meta:crash_reports.cmx`
std::string Shorten(std::string program_name);

// Builds the final report to add to the queue.
//
// * Most annotations are shared across all crash reports, e.g., |snapshot|.annotations().
// * Some annotations are report-specific, e.g., Dart exception type.
// * Adds any annotations from |report|.
//
// * Most attachments are shared across all crash reports, e.g., |snapshot|.archive().
// * Some attachments are report-specific, e.g., Dart exception stack trace.
// * Adds any attachments from |report|.
std::optional<Report> MakeReport(fuchsia::feedback::CrashReport input_report,
                                 ::fit::result<fuchsia::feedback::Snapshot, Error> snapshot,
                                 const std::optional<zx::time_utc>& current_time,
                                 const ::fit::result<std::string, Error>& device_id,
                                 const ErrorOr<std::string>& os_version, const Product& product);

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORT_UTIL_H_
