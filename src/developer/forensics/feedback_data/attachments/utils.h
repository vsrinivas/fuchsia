// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_UTILS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_UTILS_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <vector>

#include "src/developer/forensics/feedback_data/attachments/types.h"

namespace forensics {
namespace feedback_data {

// Each attachment in |attachments| that has a value will be converted into a
// fuchshia::feedback::Attachment
std::vector<fuchsia::feedback::Attachment> ToFeedbackAttachmentVector(
    const Attachments& attachments);

// Adds <|key|, |value|> to |attachments|.
void AddToAttachments(const std::string& key, const std::string& value,
                      std::vector<fuchsia::feedback::Attachment>* attachments);

// Bundles the |attachments| into a single attachment.
bool BundleAttachments(const std::vector<fuchsia::feedback::Attachment>& attachments,
                       fuchsia::feedback::Attachment* bundle);

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_UTILS_H_
