// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/main.h"

#include <fuchsia/feedback/internal/cpp/fidl.h>
#include <lib/syslog/cpp/log_settings.h>

#include "src/developer/forensics/feedback/migration/shell/main.h"

namespace forensics::feedback_data {

int main() {
  syslog::SetTags({"forensics", "feedback"});
  return ::forensics::feedback::migration_shell::internal::main<
      fuchsia::feedback::internal::FeedbackDataDirectoryMigrator>();
}

}  // namespace forensics::feedback_data
