// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DISPLAY_CAPTURE_TEST_CONTEXT_H_
#define GARNET_BIN_DISPLAY_CAPTURE_TEST_CONTEXT_H_

#include "layer.h"

namespace display_test {

class Context {
 public:
  ~Context() {}

  PrimaryLayer* CreatePrimaryLayer(uint32_t width, uint32_t height);
  Image* CreateImage(uint32_t width, uint32_t height);
  Image* CreateScalableImage(uint32_t width, uint32_t height);
  Image* CreateAlphaImage(uint32_t width, uint32_t height, uint8_t alpha, bool premultiply);

  uint32_t display_width() const;
  uint32_t display_height() const;
  bool has_frame() const { return has_frame_; }

  void SetLayers(std::vector<Layer*> layers);
  void ApplyConfig();

 private:
  Context(internal::Runner* runner) : runner_(runner) {}

  bool IsReady();

  internal::Runner* runner_;
  friend internal::Runner;

  std::vector<internal::LayerImpl*> pending_layers_;

  std::vector<std::unique_ptr<PrimaryLayer>> primary_layers_;
  std::vector<std::unique_ptr<Image>> images_;

  bool has_frame_ = false;
};

}  // namespace display_test

#endif  // GARNET_BIN_DISPLAY_CAPTURE_TEST_CONTEXT_H_
