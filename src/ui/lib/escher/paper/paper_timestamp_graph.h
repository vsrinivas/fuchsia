// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_TIMESTAMP_GRAPH_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_TIMESTAMP_GRAPH_H_

#include <string>
#include <vector>

#include "src/ui/lib/escher/debug/debug_rects.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"

namespace escher {

// Accumulates timestamps, which can then be graphed by blitting them onto a PaperRenderer.
//
// Red      render_time         Blue     random_value
// Yellow   random_value        Purple   presentation_time
class PaperTimestampGraph {
 public:
  // Used for tests.
  static constexpr int32_t kWidthPadding = 150;
  static constexpr int32_t kHeightPadding = 100;
  static constexpr int32_t kSampleLineThickness = 10;

  void AddTimestamp(PaperRenderer::Timestamp ts);

  // Draws both graph axes and content.
  void DrawOn(PaperRenderer* renderer, vk::Rect2D bounds);

  // Draws only the graph content (no axes).
  void DrawGraphContentOn(PaperRenderer* renderer, vk::Rect2D bounds);

  // Draws a graph onto the screen within the specified bounds, using DrawDebugText and Draw Line
  // calls.  Returns the bounds within which to draw the graph contents.
  vk::Rect2D DrawGraphAxesOn(PaperRenderer* renderer, vk::Rect2D bounds, std::string x_label,
                             std::string y_label, DebugRects::Color lineColor);

  void set_max_timestamp_count(size_t num);

 private:
  size_t max_timestamp_count_ = 100;
  std::vector<PaperRenderer::Timestamp> timestamps_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_TIMESTAMP_GRAPH_H_
