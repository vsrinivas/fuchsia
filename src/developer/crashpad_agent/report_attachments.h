// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ATTACHMENTS_H_
#define SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ATTACHMENTS_H_

#include <map>
#include <memory>
#include <string>

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <zircon/types.h>

#include "third_party/crashpad/client/crash_report_database.h"

namespace fuchsia {
namespace crash {

extern const char kAttachmentKernelLog[];
extern const char kAttachmentBuildInfoSnapshot[];

// Writes the kernel log to a file under |dir|.
// Returns the absolute filepath to the newly created file, empty if something
// went wrong.
std::string WriteKernelLogToFile(const std::string& dir);

// Adds the set of file attachments we want in a crash report for managed
// runtime exceptions to the |report|.
zx_status_t AddManagedRuntimeExceptionAttachments(
    crashpad::CrashReportDatabase::NewReport* report,
    ManagedRuntimeException* exception);

// Adds the set of file attachments we want in a crash report for kernel
// panics to the |report|.
//
// Today, we only attach the |crash_log| VMO as a text file attachment.
zx_status_t AddKernelPanicAttachments(
    crashpad::CrashReportDatabase::NewReport* report,
    fuchsia::mem::Buffer crash_log);

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ATTACHMENTS_H_
