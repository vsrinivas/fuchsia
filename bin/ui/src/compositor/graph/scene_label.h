// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_GRAPH_SCENE_LABEL_H_
#define APPS_MOZART_SRC_COMPOSITOR_GRAPH_SCENE_LABEL_H_

#include <string>

#include "lib/ftl/time/time_point.h"

namespace compositor {

// Convenience class for formatting descriptive labels for diagnostics.
class SceneLabel {
 public:
  SceneLabel(uint32_t token, const std::string& label);
  SceneLabel(const SceneLabel& other);
  ~SceneLabel();

  // Gets the scene token.
  uint32_t token() const { return token_; }

  // Gets the user-supplied label of the scene.
  const std::string& label() const { return label_; }

  // Gets a descriptive label including optional version and node information.
  std::string FormattedLabel() const;
  std::string FormattedLabelForVersion(uint32_t version,
                                       ftl::TimePoint presentation_time) const;
  std::string FormattedLabelForNode(uint32_t version,
                                    ftl::TimePoint presentation_time,
                                    uint32_t node_id) const;

 private:
  uint32_t const token_;
  std::string const label_;
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_GRAPH_SCENE_LABEL_H_
