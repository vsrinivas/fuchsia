// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_PROVIDER_H_

#include <lib/fpromise/promise.h>
#include <lib/zx/time.h>

#include "src/developer/forensics/feedback/attachments/types.h"

namespace forensics::feedback {

// Abstract base class for collecting attachments asynchronously.
class AttachmentProvider {
 public:
  // Returns a promise to the data collection, where collection can be terminated early with
  // |ticket|
  virtual ::fpromise::promise<AttachmentValue> Get(uint64_t ticket) = 0;

  // Completes the data collection promise associated with |ticket| early, if it hasn't
  // already completed.
  virtual void ForceCompletion(uint64_t ticket, Error error) = 0;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_PROVIDER_H_
