// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "context.h"
#include "runner.h"

namespace display_test {

PrimaryLayer* Context::CreatePrimaryLayer(uint32_t width, uint32_t height) {
  primary_layers_.push_back(
      std::unique_ptr<PrimaryLayer>(new PrimaryLayer(runner_, width, height)));
  return primary_layers_[primary_layers_.size() - 1].get();
}

Image* Context::CreateImage(uint32_t width, uint32_t height) {
  images_.push_back(std::unique_ptr<Image>(new Image(runner_, width, height)));
  return images_[images_.size() - 1].get();
}

Image* Context::CreateScalableImage(uint32_t width, uint32_t height) {
  images_.push_back(std::unique_ptr<Image>(new Image(runner_, width, height, true)));
  return images_[images_.size() - 1].get();
}

Image* Context::CreateAlphaImage(uint32_t width, uint32_t height, uint8_t alpha, bool premultiply) {
  images_.push_back(std::unique_ptr<Image>(new Image(runner_, width, height, alpha, premultiply)));
  return images_[images_.size() - 1].get();
}

void Context::SetLayers(std::vector<Layer*> layers) {
  pending_layers_.clear();
  for (auto l : layers) {
    pending_layers_.push_back(l);
  }
}

void Context::ApplyConfig() {
  has_frame_ = true;
  runner_->ApplyConfig(pending_layers_);
}

bool Context::IsReady() {
  for (auto& layer : primary_layers_) {
    if (layer->id() == 0) {
      return false;
    }
  }
  for (auto& image : images_) {
    if (image->id() == 0) {
      return false;
    }
  }
  return true;
}

uint32_t Context::display_width() const { return runner_->width(); }

uint32_t Context::display_height() const { return runner_->height(); }

}  // namespace display_test
