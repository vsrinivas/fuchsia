// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/fakes/crash_reporter.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <zircon/errors.h>

namespace feedback {
namespace fakes {

using namespace fuchsia::feedback;

void CrashReporter::File(CrashReport report, FileCallback callback) {
  if (!report.has_program_name()) {
    callback(CrashReporter_File_Result::WithErr(ZX_ERR_INVALID_ARGS));
  } else {
    callback(CrashReporter_File_Result::WithResponse(CrashReporter_File_Response()));
  }
}

}  // namespace fakes
}  // namespace feedback
