// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_REDACTOR_FACTORY_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_REDACTOR_FACTORY_H_

#include <lib/fit/function.h>
#include <lib/inspect/cpp/vmo/types.h>

#include <memory>

#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/utils/redact/redactor.h"

namespace forensics::feedback {

// Default function for generating a random starting cache id by drawing from a uniform distribution
// between 0 and 7500. This is done in an attempt to prevent collisions between the current and
// previous boot logs and keep stringified IDs between 1 and 4 digits for easy human consumption.
int DefaultCacheIdFn();

// Returns an IdentityRedactor if the file at |enable_flag_file| doesn't exist, otherwise return a
// Redactor.
std::unique_ptr<RedactorBase> RedactorFromConfig(
    inspect::Node* root_node, const std::string& enable_flag_file = kEnableRedactDataPath,
    ::fit::function<int()> seed_cache_id = DefaultCacheIdFn);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_REDACTOR_FACTORY_H_
