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
  virtual ::fpromise::promise<AttachmentValue> Get(zx::duration timeout) = 0;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_PROVIDER_H_
