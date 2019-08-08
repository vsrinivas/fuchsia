// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_REPORT_UTIL_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_REPORT_UTIL_H_

#include <fuchsia/mem/cpp/fidl.h>

#include <string>

#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/util/file/file_writer.h"

namespace fuchsia {
namespace crash {

// Writes a VMO into a Crashpad writer.
bool WriteVMO(const fuchsia::mem::Buffer& vmo, crashpad::FileWriter* writer);

// Adds a file attachment to a Crashpad report.
bool AddAttachment(const std::string& attachment_filename,
                   const fuchsia::mem::Buffer& attachment_content,
                   crashpad::CrashReportDatabase::NewReport* crashpad_report);

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASH_REPORT_UTIL_H_
