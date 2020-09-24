// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_LAYER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_LAYER_H_

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/intrusive_single_list.h>
#include <fbl/ref_ptr.h>

#include "image.h"

namespace display {

class FenceCollection;
class Layer;
class LayerTest;
class Client;

typedef struct layer_node : public fbl::SinglyLinkedListable<layer_node*> {
  Layer* layer;
} layer_node_t;

// Almost-POD used by Client to manage layer state. Public state is used by Controller.
class Layer : public IdMappable<std::unique_ptr<Layer>> {
 public:
  explicit Layer(uint64_t id);
  ~Layer();
  fbl::RefPtr<Image> current_image() const { return displayed_image_; }
  uint32_t z_order() const { return current_layer_.z_index; }
  bool is_skipped() const { return is_skipped_; }

  // TODO(fxbug.dev/42686) Although this is nominally a POD, the state management and lifecycle are
  // complicated by interactions with Client's threading model.
  friend Client;
  friend LayerTest;

  bool in_use() const { return current_node_.InContainer() || pending_node_.InContainer(); }
  const image_t* pending_image() const {
    return pending_layer_.type == LAYER_TYPE_PRIMARY ? &pending_layer_.cfg.primary.image
                                                     : &pending_layer_.cfg.cursor.image;
  }
  auto current_type() const { return current_layer_.type; }
  auto pending_type() const { return pending_layer_.type; }

  // Resolve fences and retire any displayed images. Returns false if there were any errors.
  bool ResolvePendingImage(FenceCollection* fence);

  // Make the staged config current.
  void ApplyChanges(const display_mode_t& mode);

  // Discard the pending changes
  void DiscardChanges();

  // Remove references to the provided image or all refs if image is nullptr. Returns true if the
  // current config was affected.
  bool CleanUpImage(Image* image);

  // If a new image is available, retire current_image() and other pending images. Returns false if
  // no images were ready.
  bool ActivateLatestReadyImage();

  // Adds the pending_layer_ to a display list, at z_index. Returns false if the pending_layer_ is
  // currently in use.
  bool AddToConfig(fbl::SinglyLinkedList<layer_node_t*>* list, uint32_t z_index);

  void SetPrimaryConfig(llcpp::fuchsia::hardware::display::ImageConfig image_config);
  void SetPrimaryPosition(llcpp::fuchsia::hardware::display::Transform transform,
                          llcpp::fuchsia::hardware::display::Frame src_frame,
                          llcpp::fuchsia::hardware::display::Frame dest_frame);
  void SetPrimaryAlpha(llcpp::fuchsia::hardware::display::AlphaMode mode, float val);
  void SetCursorConfig(llcpp::fuchsia::hardware::display::ImageConfig image_config);
  void SetCursorPosition(int32_t x, int32_t y);
  void SetColorConfig(uint32_t pixel_format, ::fidl::VectorView<uint8_t> color_bytes);
  void SetImage(fbl::RefPtr<Image> image_id, uint64_t wait_event_id, uint64_t signal_event_id);

 private:
  layer_t pending_layer_;
  layer_t current_layer_;
  // flag indicating that there are changes in pending_layer that
  // need to be applied to current_layer.
  bool config_change_;

  // Event ids passed to SetLayerImage which haven't been applied yet.
  uint64_t pending_wait_event_id_;
  uint64_t pending_signal_event_id_;

  // The image given to SetLayerImage which hasn't been applied yet.
  fbl::RefPtr<Image> pending_image_;

  // Image which are waiting to be displayed
  list_node_t waiting_images_ = LIST_INITIAL_VALUE(waiting_images_);
  // The image which has most recently been sent to the display controller impl
  fbl::RefPtr<Image> displayed_image_;

  // Counters used for keeping track of when the layer's images need to be dropped.
  uint64_t pending_image_config_gen_ = 0;
  uint64_t current_image_config_gen_ = 0;

  int32_t pending_cursor_x_;
  int32_t pending_cursor_y_;
  int32_t current_cursor_x_;
  int32_t current_cursor_y_;

  // Storage for a color layer's color data bytes.
  uint8_t pending_color_bytes_[4];
  uint8_t current_color_bytes_[4];

  layer_node_t pending_node_;
  layer_node_t current_node_;

  // The display this layer was most recently displayed on
  uint64_t current_display_id_;

  bool is_skipped_;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_LAYER_H_
