// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/forensics/feedback_data/attachments/provider.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics::feedback_data {

// Collects the Inspect data.
//
// fuchsia.diagnostics.FeedbackArchiveAccessor is expected to be in |services|.
class Inspect : public AttachmentProvider {
 public:
  Inspect(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
          std::unique_ptr<backoff::Backoff> backoff,
          std::optional<size_t> data_budget = std::nullopt);

  ::fpromise::promise<AttachmentValue> Get(zx::duration timeout) override;

 private:
  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<backoff::Backoff> backoff_;
  std::optional<size_t> data_budget_;

  fuchsia::diagnostics::ArchiveAccessorPtr archive_accessor_;

  fxl::WeakPtrFactory<Inspect> ptr_factory_{this};
};

}  // namespace forensics::feedback_data

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_H_
