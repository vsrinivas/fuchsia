// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/yuv_to_image_pipe/yuv_cyclic_view.h"

#include <lib/ui/scenic/cpp/commands.h>

#include <cmath>
#include <iostream>

namespace yuv_to_image_pipe {

namespace {

constexpr float kDisplayHeight = 50;

}  // namespace

YuvCyclicView::YuvCyclicView(scenic::ViewContext context,
                             fuchsia::sysmem::PixelFormatType pixel_format)
    : YuvBaseView(std::move(context), pixel_format) {
  const auto image_id = AddImage();
  PaintImage(image_id, 255);
  PresentImage(image_id);
}

void YuvCyclicView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
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
  node_.SetTranslation(kHalfWidth * static_cast<float>(1. + .1 * sin(seconds * 0.8)),
                       kHalfHeight * static_cast<float>(1. + .1 * sin(seconds * 0.6)),
                       -kDisplayHeight);

  // The recangle is constantly animating; invoke InvalidateScene() to guarantee
  // that OnSceneInvalidated() will be called again.
  InvalidateScene();
}

}  // namespace yuv_to_image_pipe
