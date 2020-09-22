// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_timestamp_graph.h"

namespace escher {

// Used to calculate the area of the debug graph that bars will be drawn in.
static constexpr int32_t kWidthPadding = 150;
static constexpr int32_t kHeightPadding = 100;

static constexpr int32_t kAxisLineThickness = 10;

void PaperTimestampGraph::AddTimestamp(PaperRenderer::Timestamp ts) {
  if (timestamps_.size() >= max_timestamp_count_) {
    for (size_t i = 1; i < timestamps_.size(); ++i) {
      timestamps_[i - 1] = timestamps_[i];
    }
    timestamps_[timestamps_.size() - 1] = ts;
  } else {
    timestamps_.push_back(ts);
  }
}

void PaperTimestampGraph::DrawOn(PaperRenderer* r, vk::Rect2D bounds) {
  // Obtain the smaller bounds that can be drawn within.
  bounds = DrawGraphAxesOn(r, bounds, "TIME", "FRAMES", DebugRects::kWhite);
  DrawGraphContentOn(r, bounds);
}

void PaperTimestampGraph::DrawGraphContentOn(PaperRenderer* r, vk::Rect2D bounds) {
  FX_DCHECK(r) << "no PaperRenderer";

  const uint32_t width = bounds.extent.width;
  const uint32_t height = bounds.extent.height;

  const int16_t x_start = bounds.offset.x + 10;
  const int16_t y_axis = bounds.offset.y + height;
  const int16_t x_axis = bounds.offset.x + width;
  const int16_t h_interval = height / 35;
  const int16_t w_interval = kSampleLineThickness;

  const int16_t middle_bar = y_axis - (h_interval * 16) + 2;

  for (std::size_t i = 0; i < timestamps_.size(); i++) {
    const auto& ts = timestamps_[i];

    int16_t render_time = ts.render_done - ts.render_start;
    int16_t presentation_time = ts.actual_present - ts.target_present;

    if (static_cast<int16_t>(x_start + (i * w_interval)) <= x_axis) {
      // TODO(fxbug.dev/43208): these are blitting over each other (they all start at y_axis).
      if (render_time != 0) {
        r->DrawVLine(escher::DebugRects::kRed, x_start + (i * w_interval), y_axis,
                     y_axis - (h_interval * render_time), w_interval);
      }
      if (ts.latch_point != 0) {
        r->DrawVLine(escher::DebugRects::kYellow, x_start + (i * w_interval), y_axis,
                     y_axis - (h_interval * ts.latch_point), w_interval);
      }
      if (ts.update_done != 0) {
        r->DrawVLine(escher::DebugRects::kBlue, x_start + (i * w_interval), y_axis,
                     y_axis - (h_interval * ts.update_done), w_interval);
      }
      if (false) {  // presentation_time != 0) {
        r->DrawVLine(escher::DebugRects::kPurple, x_start + (i * w_interval), middle_bar,
                     middle_bar - (h_interval * presentation_time), w_interval);
      }
    } else {
      // TODO(fxbug.dev/7335): Delete and replace values in array
    }
  }
}

vk::Rect2D PaperTimestampGraph::DrawGraphAxesOn(PaperRenderer* r, vk::Rect2D bounds,
                                                std::string x_label, std::string y_label,
                                                DebugRects::Color line_color) {
  FX_DCHECK(r) << "no PaperRenderer";

  const int32_t frame_width = bounds.extent.width;
  const int32_t frame_height = bounds.extent.height;

  const int32_t origin_x = bounds.offset.x + kWidthPadding;
  const int32_t origin_y = bounds.offset.y + frame_height - kHeightPadding;

  const uint16_t x_axis_length = frame_width - kWidthPadding;
  const uint16_t y_axis_length = frame_height - kHeightPadding;
  const uint16_t h_interval = (y_axis_length - kHeightPadding) / 35;

  const vk::Rect2D content_bounds{
      {bounds.offset.x + kWidthPadding, bounds.offset.y},
      {bounds.extent.width - kWidthPadding, bounds.extent.height - kHeightPadding}};

  // X-axis
  r->DrawHLine(line_color, origin_y, origin_x, bounds.offset.x + bounds.extent.width,
               kAxisLineThickness);
  r->DrawDebugText(x_label,
                   {bounds.offset.x + 5, bounds.offset.y + (frame_height - kHeightPadding) / 2}, 5);

  // Y-axis
  r->DrawVLine(line_color, origin_x, bounds.offset.y, origin_y, kAxisLineThickness);
  r->DrawDebugText(y_label, {bounds.offset.x + frame_width / 2, origin_y + 25}, 5);

  // Colored bar used to show acceptable vs concerning values (acceptable below bar).
  constexpr float kAcceptableLine = 0.6;
  r->DrawHLine(DebugRects::kGreen, origin_y - (kAcceptableLine * content_bounds.extent.height),
               origin_x + 10, content_bounds.offset.x + content_bounds.extent.width, 5);

  return content_bounds;
}

}  // namespace escher
