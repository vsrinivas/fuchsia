// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "layer.h"

#include <ddk/debug.h>
#include <fbl/auto_lock.h>

#include "fence.h"

namespace fhd = llcpp::fuchsia::hardware::display;

namespace display {

namespace {

static constexpr uint32_t kInvalidLayerType = UINT32_MAX;

// Removes and invokes EarlyRetire on all entries before end.
static void do_early_retire(list_node_t* list, display::image_node_t* end = nullptr) {
  display::image_node_t* node;
  while ((node = list_peek_head_type(list, display::image_node_t, link)) != end) {
    node->self->EarlyRetire();
    node->self.reset();
    list_remove_head(list);
  }
}

static void populate_image(const fhd::ImageConfig& image, image_t* image_out) {
  static_assert(offsetof(image_t, width) == offsetof(fhd::ImageConfig, width), "Struct mismatch");
  static_assert(offsetof(image_t, height) == offsetof(fhd::ImageConfig, height), "Struct mismatch");
  static_assert(offsetof(image_t, pixel_format) == offsetof(fhd::ImageConfig, pixel_format),
                "Struct mismatch");
  static_assert(offsetof(image_t, type) == offsetof(fhd::ImageConfig, type), "Struct mismatch");
  memcpy(image_out, &image, sizeof(fhd::ImageConfig));
}

}  // namespace

Layer::Layer(uint64_t id) {
  this->id = id;
  memset(&pending_layer_, 0, sizeof(layer_t));
  memset(&current_layer_, 0, sizeof(layer_t));
  config_change_ = false;
  pending_node_.layer = this;
  current_node_.layer = this;
  current_display_id_ = INVALID_DISPLAY_ID;
  current_layer_.type = kInvalidLayerType;
  pending_layer_.type = kInvalidLayerType;
  is_skipped_ = false;
}

Layer::~Layer() {
  if (pending_image_) {
    pending_image_->DiscardAcquire();
  }
  do_early_retire(&waiting_images_);
  if (displayed_image_) {
    fbl::AutoLock lock(displayed_image_->mtx());
    displayed_image_->StartRetire();
  }
}

bool Layer::ResolvePendingImage(FenceCollection* fences) {
  // If the layer's image configuration changed, get rid of any current images
  if (pending_image_config_gen_ != current_image_config_gen_) {
    current_image_config_gen_ = pending_image_config_gen_;

    if (pending_image_ == nullptr) {
      zxlogf(ERROR, "Tried to apply configuration with missing image\n");
      return false;
    }

    while (!list_is_empty(&waiting_images_)) {
      do_early_retire(&waiting_images_);
    }
    if (displayed_image_ != nullptr) {
      {
        fbl::AutoLock lock(displayed_image_->mtx());
        displayed_image_->StartRetire();
      }
      displayed_image_ = nullptr;
    }
  }

  if (pending_image_) {
    auto wait_fence = fences->GetFence(pending_wait_event_id_);
    if (wait_fence && wait_fence->InContainer()) {
      zxlogf(ERROR, "Tried to wait with a busy event\n");
      return false;
    }
    pending_image_->PrepareFences(std::move(wait_fence),
                                  fences->GetFence(pending_signal_event_id_));
    {
      fbl::AutoLock lock(pending_image_->mtx());
      list_add_tail(&waiting_images_, &pending_image_->node.link);
      pending_image_->node.self = std::move(pending_image_);
    }
  }
  return true;
}

void Layer::ApplyChanges(const display_mode_t& mode) {
  if (!config_change_) {
    return;
  }

  current_layer_ = pending_layer_;
  config_change_ = false;

  image_t* new_image_config = nullptr;
  if (current_layer_.type == LAYER_TYPE_PRIMARY) {
    new_image_config = &current_layer_.cfg.primary.image;
  } else if (current_layer_.type == LAYER_TYPE_CURSOR) {
    new_image_config = &current_layer_.cfg.cursor.image;

    current_cursor_x_ = pending_cursor_x_;
    current_cursor_y_ = pending_cursor_y_;

    current_layer_.cfg.cursor.x_pos =
        fbl::clamp(current_cursor_x_, -static_cast<int32_t>(new_image_config->width) + 1,
                   static_cast<int32_t>(mode.h_addressable) - 1);
    current_layer_.cfg.cursor.y_pos =
        fbl::clamp(current_cursor_y_, -static_cast<int32_t>(new_image_config->height) + 1,
                   static_cast<int32_t>(mode.v_addressable) - 1);
  } else if (current_layer_.type == LAYER_TYPE_COLOR) {
    memcpy(current_color_bytes_, pending_color_bytes_, sizeof(current_color_bytes_));
    current_layer_.cfg.color.color_list = current_color_bytes_;
    current_layer_.cfg.color.color_count = 4;
  } else {
    // type is validated in ::CheckConfig, so something must be very wrong.
    ZX_ASSERT(false);
  }

  if (new_image_config && displayed_image_) {
    new_image_config->handle = displayed_image_->info().handle;
  }
}

void Layer::DiscardChanges() {
  pending_image_config_gen_ = current_image_config_gen_;
  if (pending_image_) {
    pending_image_->DiscardAcquire();
    pending_image_ = nullptr;
  }
  if (config_change_) {
    pending_layer_ = current_layer_;
    config_change_ = false;
    pending_cursor_x_ = current_cursor_x_;
    pending_cursor_y_ = current_cursor_y_;
  }

  memcpy(pending_color_bytes_, current_color_bytes_, sizeof(pending_color_bytes_));
}

bool Layer::CleanUpImage(Image* image) {
  if (pending_image_ && (image == nullptr || pending_image_.get() == image)) {
    pending_image_->DiscardAcquire();
    pending_image_ = nullptr;
  }
  if (image == nullptr) {
    do_early_retire(&waiting_images_, nullptr);
  } else {
    image_node_t* waiting;
    list_for_every_entry (&waiting_images_, waiting, image_node_t, link) {
      if (waiting->self.get() == image) {
        list_delete(&waiting->link);
        waiting->self->EarlyRetire();
        waiting->self.reset();
        break;
      }
    }
  }
  if (displayed_image_ && (image == nullptr || displayed_image_.get() == image)) {
    {
      fbl::AutoLock lock(displayed_image_->mtx());
      displayed_image_->StartRetire();
    }
    displayed_image_ = nullptr;

    if (current_node_.InContainer()) {
      return true;
    }
  }
  return false;
}

bool Layer::ActivateLatestReadyImage() {
  image_node_t* node = list_peek_tail_type(&waiting_images_, image_node_t, link);
  while (node != nullptr && !node->self->IsReady()) {
    node = list_prev_type(&waiting_images_, &node->link, image_node_t, link);
  }
  if (node == nullptr) {
    return false;
  }

  // Retire the last active image
  if (displayed_image_ != nullptr) {
    fbl::AutoLock lock(displayed_image_->mtx());
    displayed_image_->StartRetire();
  }

  // Retire the pending images that were never presented.
  do_early_retire(&waiting_images_, node);
  displayed_image_ = std::move(node->self);
  list_remove_head(&waiting_images_);

  uint64_t handle = displayed_image_->info().handle;
  if (current_layer_.type == LAYER_TYPE_PRIMARY) {
    current_layer_.cfg.primary.image.handle = handle;
  } else if (current_layer_.type == LAYER_TYPE_CURSOR) {
    current_layer_.cfg.cursor.image.handle = handle;
  } else {
    // type is validated in Client::CheckConfig, so something must be very wrong.
    ZX_ASSERT(false);
  }
  return true;
}

bool Layer::AddToConfig(fbl::SinglyLinkedList<layer_node_t*>* list, uint32_t z_index) {
  if (pending_node_.InContainer()) {
    return false;
  } else {
    pending_layer_.z_index = z_index;
    list->push_front(&pending_node_);
    return true;
  }
}

void Layer::SetPrimaryConfig(llcpp::fuchsia::hardware::display::ImageConfig image_config) {
  pending_layer_.type = LAYER_TYPE_PRIMARY;
  auto* primary = &pending_layer_.cfg.primary;
  populate_image(image_config, &primary->image);
  const frame_t new_frame = {
      .x_pos = 0, .y_pos = 0, .width = image_config.width, .height = image_config.height};
  primary->src_frame = new_frame;
  primary->dest_frame = new_frame;
  pending_image_config_gen_++;
  pending_image_ = nullptr;
  config_change_ = true;
}

void Layer::SetPrimaryPosition(llcpp::fuchsia::hardware::display::Transform transform,
                               llcpp::fuchsia::hardware::display::Frame src_frame,
                               llcpp::fuchsia::hardware::display::Frame dest_frame) {
  primary_layer_t* primary_layer = &pending_layer_.cfg.primary;

  static_assert(sizeof(fhd::Frame) == sizeof(frame_t), "Struct mismatch");
  static_assert(offsetof(fhd::Frame, x_pos) == offsetof(frame_t, x_pos), "Struct mismatch");
  static_assert(offsetof(fhd::Frame, y_pos) == offsetof(frame_t, y_pos), "Struct mismatch");
  static_assert(offsetof(fhd::Frame, width) == offsetof(frame_t, width), "Struct mismatch");
  static_assert(offsetof(fhd::Frame, height) == offsetof(frame_t, height), "Struct mismatch");

  memcpy(&primary_layer->src_frame, &src_frame, sizeof(frame_t));
  memcpy(&primary_layer->dest_frame, &dest_frame, sizeof(frame_t));
  primary_layer->transform_mode = static_cast<uint8_t>(transform);

  config_change_ = true;
}

void Layer::SetPrimaryAlpha(llcpp::fuchsia::hardware::display::AlphaMode mode, float val) {
  primary_layer_t* primary_layer = &pending_layer_.cfg.primary;

  static_assert(static_cast<alpha_t>(fhd::AlphaMode::DISABLE) == ALPHA_DISABLE, "Bad constant");
  static_assert(static_cast<alpha_t>(fhd::AlphaMode::PREMULTIPLIED) == ALPHA_PREMULTIPLIED,
                "Bad constant");
  static_assert(static_cast<alpha_t>(fhd::AlphaMode::HW_MULTIPLY) == ALPHA_HW_MULTIPLY,
                "Bad constant");

  primary_layer->alpha_mode = static_cast<alpha_t>(mode);
  primary_layer->alpha_layer_val = val;

  config_change_ = true;
}

void Layer::SetCursorConfig(llcpp::fuchsia::hardware::display::ImageConfig image_config) {
  pending_layer_.type = LAYER_TYPE_CURSOR;
  pending_cursor_x_ = pending_cursor_y_ = 0;

  cursor_layer_t* cursor_layer = &pending_layer_.cfg.cursor;
  populate_image(image_config, &cursor_layer->image);

  pending_image_config_gen_++;
  pending_image_ = nullptr;
  config_change_ = true;
}

void Layer::SetCursorPosition(int32_t x, int32_t y) {
  pending_cursor_x_ = x;
  pending_cursor_y_ = y;

  config_change_ = true;
}

void Layer::SetColorConfig(uint32_t pixel_format, ::fidl::VectorView<uint8_t> color_bytes) {
  // Increase the size of the static array when large color formats are introduced
  ZX_ASSERT(color_bytes.count() <= sizeof(pending_color_bytes_));

  pending_layer_.type = LAYER_TYPE_COLOR;
  color_layer_t* color_layer = &pending_layer_.cfg.color;

  color_layer->format = pixel_format;
  memcpy(pending_color_bytes_, color_bytes.data(), sizeof(pending_color_bytes_));

  pending_image_ = nullptr;
  config_change_ = true;
}

void Layer::SetImage(fbl::RefPtr<Image> image, uint64_t wait_event_id, uint64_t signal_event_id) {
  if (pending_image_) {
    pending_image_->DiscardAcquire();
  }

  pending_image_ = image;
  pending_wait_event_id_ = wait_event_id;
  pending_signal_event_id_ = signal_event_id;
}

}  // namespace display
