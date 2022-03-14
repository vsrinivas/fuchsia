// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_REDACTOR_FACTORY_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_REDACTOR_FACTORY_H_

#include <memory>

#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/utils/redact/redactor.h"

namespace forensics::feedback {

// Returns an IdentityRedactor if the file at |enable_flag_file| doesn't exist, otherwise return a
// Redactor.
std::unique_ptr<RedactorBase> RedactorFromConfig(
    const std::string& enable_flag_file = kEnableRedactDataPath);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_REDACTOR_FACTORY_H_
