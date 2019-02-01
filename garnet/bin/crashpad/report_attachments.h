// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CRASHPAD_REPORT_ATTACHMENTS_H_
#define GARNET_BIN_CRASHPAD_REPORT_ATTACHMENTS_H_

#include <map>
#include <memory>
#include <string>

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <third_party/crashpad/client/crash_report_database.h>
#include <zircon/types.h>

namespace fuchsia {
namespace crash {

extern const char kAttachmentKernelLog[];
extern const char kAttachmentBuildInfoSnapshot[];

// Writes the kernel log to a file under |dir|.
// Returns the absolute filepath to the newly created file, empty if something
// went wrong.
std::string WriteKernelLogToFile(const std::string& dir);

// Adds the set of file attachments we want in a crash report for managed
// runtime exceptions for the given |language| to the |report|.
//
// Today, we only attach the |stack_trace| VMO as a text file attachment.
zx_status_t AddManagedRuntimeExceptionAttachments(
    crashpad::CrashReportDatabase::NewReport* report,
    ManagedRuntimeLanguage language, fuchsia::mem::Buffer stack_trace);

// Adds the set of file attachments we want in a crash report for kernel
// panics to the |report|.
//
// Today, we only attach the |crashlog| VMO as a text file attachment.
zx_status_t AddKernelPanicAttachments(
    crashpad::CrashReportDatabase::NewReport* report,
    fuchsia::mem::Buffer crashlog);

}  // namespace crash
}  // namespace fuchsia

#endif  // GARNET_BIN_CRASHPAD_REPORT_ATTACHMENTS_H_
