// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_TYPES_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_TYPES_H_

#include <map>
#include <set>
#include <string>

#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace feedback_data {

using AnnotationKey = std::string;
using AnnotationKeys = std::set<AnnotationKey>;
using AnnotationOr = ErrorOr<std::string>;
using Annotations = std::map<AnnotationKey, AnnotationOr>;

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_TYPES_H_
