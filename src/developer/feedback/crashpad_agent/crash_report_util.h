// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>

#include <map>
#include <optional>
#include <string>

#include "third_party/crashpad/client/crash_report_database.h"

namespace fuchsia {
namespace crash {

// Checks whether |report| is a valid fuchsia.feedback.CrashReport.
//
// In practice this means the report is one of the valid xunion types.
bool IsValid(const fuchsia::feedback::CrashReport& report);

// Extracts the program name from a fuchsia.feedback.CrashReport.
std::string ExtractProgramName(const fuchsia::feedback::CrashReport& report);

// Extracts the annotations from a fuchsia.feedback.CrashReport if present and upsert them into
// |annotations|.
//
// In the case of a Dart crash report, it also upserts the exception type and message.
void ExtractAnnotations(const fuchsia::feedback::CrashReport& report,
                        std::map<std::string, std::string>* annotations);

// Extracts the attachments from a fuchsia.feedback.CrashReport if present and upsert them into
// |crashpad_report|.
//
// In the case of a native crash report, it also upserts the minidump.
// In the case of a Dart crash report, it also upserts the exception stack trace.
void ExtractAttachments(const fuchsia::feedback::CrashReport& report,
                        crashpad::CrashReportDatabase::NewReport* crashpad_report);

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_
