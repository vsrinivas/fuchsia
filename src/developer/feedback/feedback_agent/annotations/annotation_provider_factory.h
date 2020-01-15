// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE
// file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_ANNOTATION_PROVIDER_FACTORY_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_ANNOTATION_PROVIDER_FACTORY_H_

#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/time.h>

#include <set>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"
#include "src/developer/feedback/utils/cobalt.h"

namespace feedback {

// Get the annotation providers that will collect the annotations in |allowlist_|.
std::vector<std::unique_ptr<AnnotationProvider>> GetProviders(
    const std::set<std::string>& allowlist, async_dispatcher_t* dispatcher,
    std::shared_ptr<sys::ServiceDirectory> services, zx::duration timeout,
    std::shared_ptr<Cobalt> cobalt);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_ANNOTATION_PROVIDER_FACTORY_H_
