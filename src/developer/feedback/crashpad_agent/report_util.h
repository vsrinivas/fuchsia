// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_UTIL_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_UTIL_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>

#include <map>
#include <optional>
#include <string>

#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/util/file/file_writer.h"

namespace feedback {

// Writes a VMO into a Crashpad writer.
bool WriteVMO(const fuchsia::mem::Buffer& vmo, crashpad::FileWriter* writer);

// Adds a file attachment to a Crashpad report.
bool AddAttachment(const std::string& filename, const fuchsia::mem::Buffer& content,
                   crashpad::CrashReportDatabase::NewReport* crashpad_report);

// Builds the final set of annotations and attachments to attach to the Crashpad report.
//
// * Most annotations are shared across all crash reports, e.g., |feedback_data|.annotations().
// * Some annotations are report-specific, e.g., Dart exception type.
// * Adds any annotations from |report|.
//
// * Most attachments are shared across all crash reports, e.g.,
//   |feedback_data|.attachment_bundle().
// * Some attachments are report-specific, e.g., Dart exception stack trace.
// * Adds any attachments from |report|.
void BuildAnnotationsAndAttachments(fuchsia::feedback::CrashReport report,
                                    fuchsia::feedback::Data feedback_data,
                                    std::optional<zx::time_utc> current_time,
                                    std::map<std::string, std::string>* annotations,
                                    std::map<std::string, fuchsia::mem::Buffer>* attachments,
                                    std::optional<fuchsia::mem::Buffer>* minidump);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_UTIL_H_
