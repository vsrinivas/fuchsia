// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DEFAULT_ANNOTATIONS_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DEFAULT_ANNOTATIONS_H_

#include <string>

#include "src/developer/forensics/crash_reports/annotation_map.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics::crash_reports {

ErrorOr<std::string> GetBuildVersion(const feedback::Annotations& startup_annotations);

AnnotationMap BuildDefaultAnnotations(const feedback::Annotations& startup_annotations);

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DEFAULT_ANNOTATIONS_H_
