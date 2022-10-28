// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_INSPECT_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_INSPECT_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/forensics/feedback/attachments/provider.h"
#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/feedback_data/inspect_data_budget.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics::feedback {

// Collects the Inspect data.
//
// fuchsia.diagnostics.FeedbackArchiveAccessor is expected to be in |services|.
class Inspect : public AttachmentProvider {
 public:
  Inspect(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
          std::unique_ptr<backoff::Backoff> backoff, feedback_data::InspectDataBudget* data_budget);

  ::fpromise::promise<AttachmentValue> Get(zx::duration timeout) override;

  // Returns a promise to the inspect data and allows collection to be terminated early with
  // |ticket|.
  ::fpromise::promise<AttachmentValue> Get(uint64_t ticket, zx::duration timeout);

  // Completes the inspect data collection promise associated with |ticket| early, if it hasn't
  // already completed.
  void ForceCompletion(uint64_t ticket, Error error);

 private:
  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<backoff::Backoff> backoff_;
  feedback_data::InspectDataBudget* data_budget_;

  fuchsia::diagnostics::ArchiveAccessorPtr archive_accessor_;

  uint64_t internal_ticket_{std::numeric_limits<uint64_t>::max()};
  std::map<uint64_t, ::fit::callback<void(std::optional<Error>)>> completers_;

  fxl::WeakPtrFactory<Inspect> ptr_factory_{this};
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_INSPECT_H_
