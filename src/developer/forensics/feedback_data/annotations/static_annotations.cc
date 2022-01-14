// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/static_annotations.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>
#include <string>

#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace forensics {
namespace feedback_data {
namespace {

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationBuildBoard,
    kAnnotationBuildProduct,
    kAnnotationBuildLatestCommitDate,
    kAnnotationBuildVersion,
    kAnnotationBuildVersionPreviousBoot,
    kAnnotationBuildIsDebug,
    kAnnotationDeviceBoardName,
    kAnnotationSystemBootIdCurrent,
    kAnnotationSystemBootIdPrevious,
    kAnnotationSystemLastRebootReason,
    kAnnotationSystemLastRebootUptime,
};

}  // namespace

Annotations GetStaticAnnotations(const AnnotationKeys& allowlist,
                                 const feedback::Annotations& startup_annotations) {
  Annotations annotations;

  for (const auto& key : RestrictAllowlist(allowlist, kSupportedAnnotations)) {
    if (startup_annotations.count(key) != 0) {
      annotations.insert({key, startup_annotations.at(key)});
    } else {
      annotations.insert({key, Error::kMissingValue});
    }
  }

  return annotations;
}

}  // namespace feedback_data
}  // namespace forensics
