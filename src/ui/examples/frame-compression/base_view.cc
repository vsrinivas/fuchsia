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
      next_color_offset_(height / 2),
      node_(session()) {
  // Create an ImagePipe and use it.
  const uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipe2Cmd(image_pipe_id, image_pipe_.NewRequest()));
  // Make sure that |image_pipe_| is created by flushing the enqueued calls.
  session()->Present(0, [](fuchsia::images::PresentationInfo info) {});

  // Create a material that has our image pipe mapped onto it:
  scenic::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rectangle shape to display on.
  scenic::Rectangle shape(session(), width_, height_);

  node_.SetShape(shape);
  node_.SetMaterial(material);
  root_node().AddChild(node_);

  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, -kDisplayHeight);
  InvalidateScene();
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

void BaseView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  // Compute the amount of time that has elapsed since the view was created.
  double seconds = static_cast<double>(presentation_info.presentation_time) / 1'000'000'000;

  const float kHalfWidth = logical_size().x * 0.5f;
  const float kHalfHeight = logical_size().y * 0.5f;

  // Compute the translation for the window to swirl around the screen.
  // Why do this?  Well, this is an example of what a View can do, and it helps
  // debug to know if scenic is still running.
  node_.SetTranslation(kHalfWidth * (1. + .1 * sin(seconds * 0.8)),
                       kHalfHeight * (1. + .1 * sin(seconds * 0.6)), -kDisplayHeight);

  // The rectangle is constantly animating; invoke InvalidateScene() to guarantee
  // that OnSceneInvalidated() will be called again.
  InvalidateScene();
}

}  // namespace frame_compression
