// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/graph/scene_label.h"

#include <cinttypes>

#include "lib/ftl/strings/string_printf.h"

namespace compositor {

SceneLabel::SceneLabel(uint32_t token, const std::string& label)
    : token_(token), label_(label) {}

SceneLabel::SceneLabel(const SceneLabel& other)
    : token_(other.token_), label_(other.label_) {}

SceneLabel::~SceneLabel() {}

std::string SceneLabel::FormattedLabel() const {
  return label_.empty() ? ftl::StringPrintf("<S%d>", token_)
                        : ftl::StringPrintf("<S%d:%s>", token_, label_.c_str());
}

std::string SceneLabel::FormattedLabelForVersion(
    uint32_t version,
    ftl::TimePoint presentation_time) const {
  return label_.empty()
             ? ftl::StringPrintf("<S%d/v%d@%f>", token_, version,
                                 presentation_time.ToEpochDelta().ToSecondsF())
             : ftl::StringPrintf("<S%d:%s/v%d@%f>", token_, label_.c_str(),
                                 version,
                                 presentation_time.ToEpochDelta().ToSecondsF());
}

std::string SceneLabel::FormattedLabelForNode(uint32_t version,
                                              ftl::TimePoint presentation_time,
                                              uint32_t node_id) const {
  return label_.empty()
             ? ftl::StringPrintf("<S%d/v%d@%f>[#%d]", token_, version,
                                 presentation_time.ToEpochDelta().ToSecondsF(),
                                 node_id)
             : ftl::StringPrintf(
                   "<S%d:%s/v%d@%f>[#%d]", token_, label_.c_str(), version,
                   presentation_time.ToEpochDelta().ToSecondsF(), node_id);
}

}  // namespace compositor
