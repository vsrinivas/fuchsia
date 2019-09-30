// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_REPORT_UTIL_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_REPORT_UTIL_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>

#include <map>
#include <string>

#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/util/file/file_writer.h"

namespace feedback {

// Writes a VMO into a Crashpad writer.
bool WriteVMO(const fuchsia::mem::Buffer& vmo, crashpad::FileWriter* writer);

// Adds a file attachment to a Crashpad report.
bool AddAttachment(const std::string& attachment_filename,
                   const fuchsia::mem::Buffer& attachment_content,
                   crashpad::CrashReportDatabase::NewReport* crashpad_report);

// Builds the final set of annotations and attachments to attach to the Crashpad report. Annotations
// are written to |annotations| and attachments to |crashpad_report|.
//
// * Most annotations are shared across all crash reports, e.g., |feedback_data|.annotations().
// * Some annotations are report-specific, e.g., Dart exception type.
// * Adds any annotations from |report|.
//
// * Most attachments are shared across all crash reports, e.g.,
//   |feedback_data|.attachment_bundle().
// * Some attachments are report-specific, e.g., Dart exception stack trace.
// * Adds any attachments from |report|.
//
// |has_minidump| indicates whether we wrote a minidump in |crashpad_report|.
void BuildAnnotationsAndAttachments(const fuchsia::feedback::CrashReport& report,
                                    const fuchsia::feedback::Data& feedback_data,
                                    std::map<std::string, std::string>* annotations,
                                    crashpad::CrashReportDatabase::NewReport* crashpad_report,
                                    bool* has_minidump);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_REPORT_UTIL_H_
