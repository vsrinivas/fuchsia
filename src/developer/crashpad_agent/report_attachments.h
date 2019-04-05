// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ATTACHMENTS_H_
#define SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ATTACHMENTS_H_

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>

#include "third_party/crashpad/client/crash_report_database.h"

namespace fuchsia {
namespace crash {

// Adds the set of file attachments we want in a crash report for managed
// runtime exceptions to the |report|.
//
// |feedback_data| may contain attachments that are shared with other
// feedback reports, e.g., user feedback reports.
void AddManagedRuntimeExceptionAttachments(
    crashpad::CrashReportDatabase::NewReport* report,
    const fuchsia::feedback::Data& feedback_data,
    ManagedRuntimeException* exception);

// Adds the set of file attachments we want in a crash report for kernel
// panics to the |report|.
//
// |feedback_data| may contain attachments that are shared with other
// feedback reports, e.g., user feedback reports.
void AddKernelPanicAttachments(crashpad::CrashReportDatabase::NewReport* report,
                               const fuchsia::feedback::Data& feedback_data,
                               fuchsia::mem::Buffer crash_log);

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ATTACHMENTS_H_
