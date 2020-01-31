// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/feedback_id.h"

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/uuid/uuid.h"

namespace feedback {

bool InitializeFeedbackId(const std::string& path) {
  if (files::IsDirectory(path)) {
    FX_LOGS(ERROR) << fxl::StringPrintf("Unable to initialize feedback id, '%s' is a directory",
                                        path.c_str());
    return false;
  }

  std::string id;
  if (files::ReadFileToString(path, &id) && uuid::IsValid(id)) {
    return true;
  }

  id = uuid::Generate();
  if (!uuid::IsValid(id) || !files::WriteFile(path, id.c_str(), id.size())) {
    FX_LOGS(ERROR) << fxl::StringPrintf("Cannot write feedback id '%s' to '%s'", id.c_str(),
                                        path.c_str());
    return false;
  }

  return true;
}

}  // namespace feedback
