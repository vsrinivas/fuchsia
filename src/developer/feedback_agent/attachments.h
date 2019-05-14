// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_ATTACHMENTS_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_ATTACHMENTS_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <set>
#include <string>
#include <vector>

namespace fuchsia {
namespace feedback {

// Returns attachments useful to attach in feedback reports (crash or user
// feedback).
//
// Only attachments which keys are in the |whitelist| will be returned.
std::vector<fit::promise<Attachment>> GetAttachments(
    std::shared_ptr<::sys::ServiceDirectory> services,
    const std::set<std::string>& whitelist);

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_ATTACHMENTS_H_
