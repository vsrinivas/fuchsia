// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/utils/cobalt.h"

namespace feedback {

// Returns annotations useful to attach in feedback reports (crash, user feedback or bug reports).
//
// * only annotations which keys are in the |allowlist| will be returned.
// * |timeout| is per annotation.
std::vector<fit::promise<Annotations>> GetAnnotations(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const AnnotationKeys& allowlist, zx::duration timeout, Cobalt* cobalt);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_H_
