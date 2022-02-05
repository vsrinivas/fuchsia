// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/default_annotations.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace forensics::crash_reports {

ErrorOr<std::string> GetBuildVersion(const feedback::Annotations& startup_annotations) {
  if (startup_annotations.count(feedback::kBuildVersionKey) != 0) {
    return startup_annotations.at(feedback::kBuildVersionKey);
  }

  return Error::kMissingValue;
}

AnnotationMap BuildDefaultAnnotations(const feedback::Annotations& startup_annotations) {
  auto GetFromStartup = [&startup_annotations](const std::string& key) -> ErrorOr<std::string> {
    if (startup_annotations.count(key) != 0) {
      return startup_annotations.at(key);
    }

    return Error::kMissingValue;
  };

  // TODO(fxbug.dev/70398): Share annotations with feedback_data so synchonous and constant
  // annotations can be added to crash reports.
  AnnotationMap default_annotations;
  default_annotations.Set(feedback::kOSNameKey, "Fuchsia")
      .Set(feedback::kOSVersionKey, GetFromStartup(feedback::kBuildVersionKey))
      .Set(feedback::kBuildVersionKey, GetFromStartup(feedback::kBuildVersionKey))
      .Set(feedback::kBuildBoardKey, GetFromStartup(feedback::kBuildBoardKey))
      .Set(feedback::kBuildProductKey, GetFromStartup(feedback::kBuildProductKey))
      .Set(feedback::kBuildLatestCommitDateKey,
           GetFromStartup(feedback::kBuildLatestCommitDateKey));

  return default_annotations;
}

}  // namespace forensics::crash_reports
