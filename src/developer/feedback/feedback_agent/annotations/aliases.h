// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_ALIASES_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_ALIASES_H_

#include <map>
#include <set>
#include <string>

namespace feedback {

using AnnotationKey = std::string;
using AnnotationKeys = std::set<AnnotationKey>;

using AnnotationValue = std::string;

using Annotations = std::map<AnnotationKey, AnnotationValue>;

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_ALIASES_H_
