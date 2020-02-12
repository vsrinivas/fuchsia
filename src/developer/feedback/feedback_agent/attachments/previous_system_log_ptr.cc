// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/previous_system_log_ptr.h"

#include "lib/fit/promise.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog//cpp/logger.h"

namespace feedback {

fit::promise<fuchsia::mem::Buffer> CollectPreviousSystemLog() {
  fsl::SizedVmo vmo;
  if (!fsl::VmoFromFilename(kPreviousLogsFilePath, &vmo)) {
    FX_LOGS(ERROR) << "Unable to convert previous logs to vmo.";
    return fit::make_result_promise<fuchsia::mem::Buffer>(fit::error());
  }

  return fit::make_result_promise<fuchsia::mem::Buffer>(fit::ok(std::move(vmo).ToTransport()));
}

}  // namespace feedback
