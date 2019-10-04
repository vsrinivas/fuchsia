// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>

#include <map>
#include <optional>
#include <string>

namespace feedback {

// Extracts the annotations and attachments from a fuchsia.feedback.CrashReport if present.
//
// In the case of a native crash report, it also upserts the minidump.
// In the case of a Dart crash report, it also upserts the exception type, message and stack trace.
void ExtractAnnotationsAndAttachments(fuchsia::feedback::CrashReport report,
                                      std::map<std::string, std::string>* annotations,
                                      std::map<std::string, fuchsia::mem::Buffer>* attachments,
                                      std::optional<fuchsia::mem::Buffer>* minidump);
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_
