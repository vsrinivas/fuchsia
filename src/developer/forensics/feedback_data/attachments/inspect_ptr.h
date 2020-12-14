// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_PTR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_PTR_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/utils/fit/timeout.h"

namespace forensics {
namespace feedback_data {

// Collects the Inspect data.
//
// fuchsia.diagnostics.Archive is expected to be in |services|.
::fit::promise<AttachmentValue> CollectInspectData(async_dispatcher_t* dispatcher,
                                                   std::shared_ptr<sys::ServiceDirectory> services,
                                                   fit::Timeout timeout,
                                                   std::optional<size_t> data_budget);

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_PTR_H_
