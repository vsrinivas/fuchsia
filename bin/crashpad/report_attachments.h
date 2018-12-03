// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CRASHPAD_REPORT_ATTACHMENTS_H_
#define GARNET_BIN_CRASHPAD_REPORT_ATTACHMENTS_H_

#include <map>
#include <memory>
#include <string>

#include <fuchsia/mem/cpp/fidl.h>
#include <sys/types.h>
#include <third_party/crashpad/client/crash_report_database.h>

#include "scoped_unlink.h"

namespace fuchsia {
namespace crash {

// Returns the set of file attachments we want in a crash report for native
// exceptions.
//
// |tmp_dir| is used to locally store the attachments until upload to the remote
// crash server.
std::map<std::string, ScopedUnlink> MakeNativeExceptionAttachments(
    const std::string& tmp_dir);

// Writes the set of file attachments we want in a crash report for kernel
// panics.
//
// Today, we only attach the |crashlog| VMO as a text file attachment.
zx_status_t WriteKernelPanicAttachments(
    crashpad::CrashReportDatabase::NewReport* report,
    fuchsia::mem::Buffer crashlog);

}  // namespace crash
}  // namespace fuchsia

#endif  // GARNET_BIN_CRASHPAD_REPORT_ATTACHMENTS_H_
