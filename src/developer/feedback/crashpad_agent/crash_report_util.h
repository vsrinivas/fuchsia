// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <map>
#include <string>

#include "third_party/crashpad/client/crash_report_database.h"

namespace feedback {

// Extracts the annotations and attachments from a fuchsia.feedback.CrashReport if present and
// upsert them into |annotations| and |crashpad_report| respectively.
//
// In the case of a native crash report, it also upserts the minidump and sets |has_minidump|.
// In the case of a Dart crash report, it also upserts the exception type, message and stack trace.
void ExtractAnnotationsAndAttachments(const fuchsia::feedback::CrashReport& report,
                                      std::map<std::string, std::string>* annotations,
                                      crashpad::CrashReportDatabase::NewReport* crashpad_report,
                                      bool* has_minidump);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_
