// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/redactor_factory.h"

#include "src/lib/files/file.h"

namespace forensics::feedback {

// Returns an IdentityRedactor if the file at |enable_flag_file| doesn't exist, otherwise return a
// Redactor.
std::unique_ptr<RedactorBase> RedactorFromConfig(const std::string& enable_flag_file) {
  if (files::IsFile(enable_flag_file)) {
    return std::unique_ptr<RedactorBase>(new Redactor);
  } else {
    return std::unique_ptr<RedactorBase>(new IdentityRedactor);
  }
}

}  // namespace forensics::feedback
