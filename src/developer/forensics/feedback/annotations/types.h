// Copyright 2022 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TYPES_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TYPES_H_

#include <map>
#include <string>

#include "src/developer/forensics/utils/errors.h"

namespace forensics::feedback {

using Annotations = std::map<std::string, ErrorOr<std::string>>;

}

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TYPES_H_
