// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_STATIC_ATTACHMENTS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_STATIC_ATTACHMENTS_H_

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/utils/cobalt/logger.h"

namespace forensics {
namespace feedback_data {

// Synchronously fetches the static attachments, i.e. the attachments that don't change during a
// boot cycle.
Attachments GetStaticAttachments(const AttachmentKeys& allowlist, cobalt::Logger* cobalt,
                                 bool is_first_instance);

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_STATIC_ATTACHMENTS_H_
