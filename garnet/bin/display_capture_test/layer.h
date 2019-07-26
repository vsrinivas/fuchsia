// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DISPLAY_CAPTURE_TEST_LAYER_H_
#define GARNET_BIN_DISPLAY_CAPTURE_TEST_LAYER_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <inttypes.h>

#include "image.h"

namespace display_test {

namespace internal {

class Runner;

class LayerImpl {
 public:
  LayerImpl(Runner* runner);
  virtual ~LayerImpl() {}

  virtual const void* ApplyState() = 0;
  virtual void SendState(const void* state) const = 0;
  virtual void DeleteState(const void* state) const = 0;
  virtual bool GetPixel(const void* state, uint32_t x, uint32_t y, uint32_t* value_out,
                        bool* skip) const = 0;
  uint64_t id() const { return id_; }

  virtual uint64_t image_id(const void* state) const = 0;

 protected:
  fuchsia::hardware::display::ControllerPtr& controller() const;
  const ImageImpl* get_image_impl(const Image* image) const;

 private:
  Runner* runner_;
  uint64_t id_ = 0;
};

}  // namespace internal

class Context;
class PrimaryLayer;
class CursorLayer;
class ColorLayer;

class Layer : internal::LayerImpl {
 private:
  Layer(internal::Runner* runner) : internal::LayerImpl(runner) {}
  virtual ~Layer() {}

  friend Context;
  friend PrimaryLayer;
  friend CursorLayer;
  friend ColorLayer;
};

class PrimaryLayer : public Layer {
 public:
  ~PrimaryLayer() {}

  void SetPosition(fuchsia::hardware::display::Transform transform,
                   fuchsia::hardware::display::Frame src, fuchsia::hardware::display::Frame dest);
  void SetAlpha(fuchsia::hardware::display::AlphaMode mode, float val);
  void SetImage(const Image* image);

 private:
  PrimaryLayer(internal::Runner* runner, uint32_t width, uint32_t height);

  // Gets the alpha multiplied color of the pixel. If the color is uncertain
  // due to scaling, skip_out will be true.
  bool GetPixel(const void* state, uint32_t x, uint32_t y, uint32_t* value_out,
                bool* skip_out) const override;
  const void* ApplyState() override;
  void SendState(const void* state) const override;
  void DeleteState(const void* state) const override;
  uint64_t image_id(const void* state) const override;

  fuchsia::hardware::display::ImageConfig config_;

  struct state {
    bool set_config = true;
    bool set_position;
    fuchsia::hardware::display::Transform transform;
    fuchsia::hardware::display::Frame src;
    fuchsia::hardware::display::Frame dest;
    bool set_alpha;
    fuchsia::hardware::display::AlphaMode alpha_mode;
    float alpha_val;
    bool flip_image;
    const internal::ImageImpl* image;
  };

  state pending_state_;

  friend Context;
};

// TODO(stevensd): Add CursorLayer and ColorLayer

}  // namespace display_test

#endif  // GARNET_BIN_DISPLAY_CAPTURE_TEST_LAYER_H_
