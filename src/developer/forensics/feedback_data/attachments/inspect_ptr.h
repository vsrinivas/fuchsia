// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_PTR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_PTR_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/utils/fidl/oneshot_ptr.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace feedback_data {

// Collects the Inspect data.
//
// fuchsia.diagnostics.Archive is expected to be in |services|.
::fit::promise<AttachmentValue> CollectInspectData(async_dispatcher_t* dispatcher,
                                                   std::shared_ptr<sys::ServiceDirectory> services,
                                                   fit::Timeout timeout);

// Wraps around fuchsia::diagnostics::ArchiveAccessorPtr, fuchsia::diagnostics::ReaderPtr and
// fuchsia::diagnostics::BatchIteratorPtr to handle establishing the connection, losing the
// connection, waiting for the callback, enforcing a timeout, etc.
//
// Collect() is expected to be called exactly once.
class Inspect {
 public:
  Inspect(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  ::fit::promise<AttachmentValue> Collect(fit::Timeout timeout);

 private:
  void SetUp();
  void StreamInspectSnapshot();
  void AppendNextInspectBatch();

  fidl::OneShotPtr<fuchsia::diagnostics::ArchiveAccessor> archive_;
  fuchsia::diagnostics::BatchIteratorPtr snapshot_iterator_;

  // Accumulated Inspect data. Each element corresponds to one valid Inspect "block" in JSON format.
  // A block would typically be the Inspect data for one component.
  std::vector<std::string> inspect_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Inspect);
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_PTR_H_
