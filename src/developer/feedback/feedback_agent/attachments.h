// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/developer/feedback/utils/cobalt.h"

namespace feedback {

// Returns attachments useful to attach in feedback reports (crash or user feedback).
//
// * only attachments which keys are in the |allowlist| will be returned.
// * |timeout| is per attachment.
std::vector<fit::promise<fuchsia::feedback::Attachment>> GetAttachments(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const std::set<std::string>& allowlist, zx::duration timeout, std::shared_ptr<Cobalt> cobalt,
    async::Executor* inspect_executor);

// Adds the |annotations| as an extra JSON attachment to |attachments|.
void AddAnnotationsAsExtraAttachment(const std::vector<fuchsia::feedback::Annotation>& annotations,
                                     std::vector<fuchsia::feedback::Attachment>* attachments);

// Bundles the attachments into a single attachment.
bool BundleAttachments(const std::vector<fuchsia::feedback::Attachment>& attachments,
                       fuchsia::feedback::Attachment* bundle);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_H_
