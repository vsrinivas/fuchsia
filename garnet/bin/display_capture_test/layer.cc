// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "layer.h"
#include <math.h>
#include "runner.h"
#include "utils.h"

namespace {

uint32_t scale(uint32_t x, uint32_t from_limit, uint32_t to_limit) {
  if (from_limit == to_limit) {
    return x;
  } else {
    return (x * to_limit + (from_limit - 1)) / from_limit;
  }
}

}  // namespace

namespace display_test {
namespace internal {

LayerImpl::LayerImpl(Runner* runner) : runner_(runner) {
  runner->display()->CreateLayer([this](zx_status_t status, uint64_t id) {
    ZX_ASSERT(status == ZX_OK);
    id_ = id;
    runner_->OnResourceReady();
  });
}

fuchsia::hardware::display::ControllerPtr& LayerImpl::controller() const {
  return runner_->display();
}

}  // namespace internal

PrimaryLayer::PrimaryLayer(internal::Runner* runner, uint32_t width, uint32_t height)
    : Layer(runner) {
  config_ = {};
  config_.width = width;
  config_.height = height;
  config_.pixel_format = internal::ImageImpl::kFormat;

  pending_state_.set_position = false;
  pending_state_.transform = fuchsia::hardware::display::Transform::IDENTITY;
  pending_state_.src = {};
  pending_state_.src.width = width;
  pending_state_.src.height = height;
  pending_state_.dest = pending_state_.src;
  pending_state_.set_alpha = false;
  pending_state_.alpha_mode = fuchsia::hardware::display::AlphaMode::DISABLE;
  pending_state_.image = nullptr;
  pending_state_.flip_image = false;
}

void PrimaryLayer::SetImage(const Image* image) {
  pending_state_.image = internal::ImageImpl::GetImageImpl(image);
  pending_state_.flip_image = true;
}

void PrimaryLayer::SetPosition(fuchsia::hardware::display::Transform transform,
                               fuchsia::hardware::display::Frame src,
                               fuchsia::hardware::display::Frame dest) {
  src.x_pos = src.x_pos & ~1;
  dest.x_pos = dest.x_pos & ~1;
  src.width = src.width & ~1;
  dest.width = dest.width & ~1;

  ZX_ASSERT(src.width == dest.width || dest.width >= kMinScalableImageSize);
  ZX_ASSERT(src.height == dest.height || dest.height >= kMinScalableImageSize);

  pending_state_.transform = transform;
  pending_state_.src = src;
  pending_state_.dest = dest;
  pending_state_.set_position = true;
}

void PrimaryLayer::SetAlpha(fuchsia::hardware::display::AlphaMode mode, float val) {
  pending_state_.alpha_mode = mode;
  pending_state_.alpha_val = val;
  pending_state_.set_alpha = true;
}

bool PrimaryLayer::GetPixel(const void* state, uint32_t x, uint32_t y, uint32_t* value_out,
                            bool* skip_out) const {
  auto s = static_cast<const struct state*>(state);

  // Transform the global x,y coordinate to a dest-frame coordinate
  x -= s->dest.x_pos;
  y -= s->dest.y_pos;
  if (x >= s->dest.width || y >= s->dest.height) {
    return false;
  }

  uint32_t dest_width = s->dest.width;
  uint32_t dest_height = s->dest.height;

  // Undo x reflection if necessary
  if (s->transform == fuchsia::hardware::display::Transform::REFLECT_X ||
      s->transform == fuchsia::hardware::display::Transform::ROT_180 ||
      s->transform == fuchsia::hardware::display::Transform::ROT_270 ||
      s->transform == fuchsia::hardware::display::Transform::ROT_90_REFLECT_X) {
    x = (dest_width - 1) - x;
  }

  // Undo y reflection if necessary
  if (s->transform == fuchsia::hardware::display::Transform::REFLECT_Y ||
      s->transform == fuchsia::hardware::display::Transform::ROT_180 ||
      s->transform == fuchsia::hardware::display::Transform::ROT_270 ||
      s->transform == fuchsia::hardware::display::Transform::ROT_90_REFLECT_Y) {
    y = (dest_height - 1) - y;
  }

  // Undo 90-degree counterclockwise rotation if necessary
  if (s->transform == fuchsia::hardware::display::Transform::ROT_90 ||
      s->transform == fuchsia::hardware::display::Transform::ROT_90_REFLECT_X ||
      s->transform == fuchsia::hardware::display::Transform::ROT_90_REFLECT_Y ||
      s->transform == fuchsia::hardware::display::Transform::ROT_270) {
    dest_width = s->dest.height;
    dest_height = s->dest.width;
    uint32_t tmp = x;
    x = (dest_width - 1) - y;
    y = tmp;
  }

  // Scale from dest-coords back to src-coords
  x = scale(x, dest_width, s->src.width);
  y = scale(y, dest_height, s->src.height);

  // If we're scaling, skip over pixels that depend on hardware interpolation
  if (dest_width != s->src.width || dest_height != s->src.height) {
    constexpr uint32_t bounds = kMinScalableImageSize / 4;
    if ((x < bounds || dest_width - bounds <= x) && (y < bounds || dest_height - bounds <= y)) {
      *skip_out = false;
    } else {
      *skip_out = true;
    }
  } else {
    *skip_out = false;
  }

  // Now we can actually get the image pixel data
  uint32_t val = s->image->get_pixel(s->src.x_pos + x, s->src.y_pos + y);

  // If there's plane alpha, add combine it with the image pixel
  if (!isnan(s->alpha_val)) {
    uint8_t plane_alpha = static_cast<uint8_t>(round(s->alpha_val * 255));
    uint32_t pixel_alpha = val >> 24;
    pixel_alpha = ((pixel_alpha * plane_alpha) + 254) >> 8;
    val = (val & ~(0xff000000)) | (pixel_alpha << 24);

    // If the mode is premultiplied, the hardware is supposed to
    // premultiply the alpha value before blending.
    if (s->alpha_mode == fuchsia::hardware::display::AlphaMode::PREMULTIPLIED) {
      val = internal::premultiply_color_channels(val, plane_alpha);
    }
  }

  if (s->alpha_mode == fuchsia::hardware::display::AlphaMode::DISABLE) {
    // Clobber the alpha value if it's disabled
    val |= 0xff000000;
  } else if (s->alpha_mode == fuchsia::hardware::display::AlphaMode::HW_MULTIPLY) {
    // If blending is hwmultiply, then do the the channel multiplication
    // here so the caller of GetPixel can treat everything is premultiplied.
    val = internal::premultiply_color_channels(val, val >> 24);
  }

  *value_out = val;
  return true;
}

const void* PrimaryLayer::ApplyState() {
  auto state = new struct state;
  *state = pending_state_;
  pending_state_.set_config = false;
  pending_state_.set_position = false;
  pending_state_.set_alpha = false;
  pending_state_.flip_image = false;

  if (state->src.height != state->dest.height || state->src.width != state->dest.width) {
    ZX_ASSERT(state->image->is_scalable());
  }
  return state;
}

void PrimaryLayer::SendState(const void* state) const {
  auto s = static_cast<const struct state*>(state);
  if (s->set_config) {
    controller()->SetLayerPrimaryConfig(id(), config_);
  }
  if (s->set_position) {
    controller()->SetLayerPrimaryPosition(id(), s->transform, s->src, s->dest);
  }
  if (s->set_alpha) {
    controller()->SetLayerPrimaryAlpha(id(), s->alpha_mode, s->alpha_val);
  }
  if (s->flip_image) {
    controller()->SetLayerImage(id(), s->image->id(), 0, 0);
  }
}

void PrimaryLayer::DeleteState(const void* state) const {
  delete static_cast<const struct state*>(state);
}

uint64_t PrimaryLayer::image_id(const void* state) const {
  auto image = static_cast<const struct state*>(state)->image;
  return image ? image->id() : 0;
}

}  // namespace display_test
