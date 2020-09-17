// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base_view.h"

#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/commands.h>

#include <cmath>

namespace frame_compression {

namespace {

constexpr float kDisplayHeight = 50;
constexpr float kInitialWindowXPos = 320;
constexpr float kInitialWindowYPos = 240;

}  // namespace

BaseView::BaseView(scenic::ViewContext context, const std::string& debug_name, uint32_t width,
                   uint32_t height)
    : scenic::BaseView(std::move(context), debug_name),
      width_(width),
      height_(height),
      material_(session()),
      next_color_offset_(height / 2),
      node_(session()) {
  // Create a rectangle shape to display on.
  scenic::Rectangle shape(session(), width_, height_);

  node_.SetShape(shape);
  node_.SetMaterial(material_);
  root_node().AddChild(node_);

  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, -kDisplayHeight);
}

uint32_t BaseView::GetNextImageIndex() {
  const auto rv = next_image_index_;
  next_image_index_ = (next_image_index_ + 1) % kNumImages;
  return rv;
}

uint32_t BaseView::GetNextColorOffset() {
  const auto rv = next_color_offset_;
  next_color_offset_ = (next_color_offset_ + 1) % height_;
  return rv;
}

uint32_t BaseView::GetNextFrameNumber() {
  const auto rv = next_frame_number_;
  next_frame_number_ = next_frame_number_ + 1;
  return rv;
}

void BaseView::Animate(fuchsia::images::PresentationInfo presentation_info) {
  // Compute the amount of time that has elapsed since the view was created.
  double seconds = static_cast<double>(presentation_info.presentation_time) / 1'000'000'000;

  const float kHalfWidth = logical_size().x * 0.5f;
  const float kHalfHeight = logical_size().y * 0.5f;

  // Compute the translation for the window to swirl around the screen.
  node_.SetTranslation(kHalfWidth * (1. + .1 * sin(seconds * 0.8)),
                       kHalfHeight * (1. + .1 * sin(seconds * 0.6)), -kDisplayHeight);
}

}  // namespace frame_compression
