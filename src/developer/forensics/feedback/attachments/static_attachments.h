// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_STATIC_ATTACHMENTS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_STATIC_ATTACHMENTS_H_

#include "src/developer/forensics/feedback/attachments/types.h"

namespace forensics::feedback {

// Synchronously fetches the static attachments, i.e. the attachments that don't change during a
// boot cycle.
Attachments GetStaticAttachments();

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_STATIC_ATTACHMENTS_H_
