// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_INSPECT_PTR_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_INSPECT_PTR_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/time.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/attachments/aliases.h"
#include "src/developer/feedback/utils/bridge.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Collects the Inspect data.
//
// fuchsia.diagnostics.Archive is expected to be in |services|.
fit::promise<AttachmentValue> CollectInspectData(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services,
                                                 zx::duration timeout, Cobalt* cobalt);

// Wraps around fuchsia::diagnostics::ArchivePtr, fuchsia::diagnostics::ReaderPtr and
// fuchsia::diagnostics::BatchIteratorPtr to handle establishing the connection, losing the
// connection, waiting for the callback, enforcing a timeout, etc.
//
// Collect() is expected to be called exactly once.
class Inspect {
 public:
  Inspect(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
          Cobalt* cobalt);

  fit::promise<AttachmentValue> Collect(zx::duration timeout);

 private:
  void SetUp();
  void StreamInspectSnapshot();
  void AppendNextInspectBatch();

  const std::shared_ptr<sys::ServiceDirectory> services_;
  Cobalt* cobalt_;

  // Enforces the one-shot nature of Collect().
  bool has_called_collect_ = false;

  fuchsia::diagnostics::ArchivePtr archive_;
  fuchsia::diagnostics::BatchIteratorPtr snapshot_iterator_;

  // Accumulated Inspect data. Each element corresponds to one valid Inspect "block" in JSON format.
  // A block would typically be the Inspect data for one component.
  std::vector<std::string> inspect_data_;

  Bridge<> bridge_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Inspect);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_INSPECT_PTR_H_
