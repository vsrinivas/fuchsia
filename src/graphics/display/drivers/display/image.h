// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_IMAGE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_IMAGE_H_

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/zx/vmo.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <atomic>

#include <ddk/protocol/display/controller.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "fence.h"
#include "id-map.h"

namespace display {

class Controller;

typedef struct image_node {
  list_node_t link;
  fbl::RefPtr<class Image> self;
} image_node_t;

class Image : public fbl::RefCounted<Image>, public IdMappable<fbl::RefPtr<Image>> {
 public:
  Image(Controller* controller, const image_t& info, zx::vmo vmo, uint32_t stride_px);
  Image(Controller* controller, const image_t& info);
  ~Image();

  image_t& info() { return info_; }
  uint32_t stride_px() const { return stride_px_; }

  // Marks the image as in use.
  bool Acquire();
  // Marks the image as not in use. Should only be called before PrepareFences.
  void DiscardAcquire();
  // Called to set this image's fences and prepare the image to be displayed.
  void PrepareFences(fbl::RefPtr<FenceReference>&& wait, fbl::RefPtr<FenceReference>&& signal);
  // Called to immediately retire the image if StartPresent hasn't been called yet.
  void EarlyRetire();
  // Called when the image is passed to the display hardware.
  void StartPresent() __TA_REQUIRES(mtx());
  // Called when another image is presented after this one.
  void StartRetire() __TA_REQUIRES(mtx());
  // Called on vsync after StartRetire has been called.
  void OnRetire() __TA_REQUIRES(mtx());

  // Called on all waiting images when any fence fires. Returns true if the image is ready to present.
  bool OnFenceReady(FenceReference* fence);

  // Called to reset fences when client releases the image. Releasing fences
  // is independent of the rest of the image lifecycle.
  void ResetFences() __TA_REQUIRES(mtx());

  bool IsReady() const { return wait_fence_ == nullptr; }

  bool HasSameConfig(const image_t& config) const {
    return info_.width == config.width && info_.height == config.height &&
           info_.pixel_format == config.pixel_format && info_.type == config.type;
  }
  bool HasSameConfig(const Image& other) const { return HasSameConfig(other.info_); }

  const zx::vmo& vmo() { return vmo_; }

  void set_z_index(uint32_t z_index) { z_index_ = z_index; }
  uint32_t z_index() const { return z_index_; }

  // The node alternates between a client's waiting image list and the controller's
  // presented image list. The presented image list is protected with the controller mutex,
  // and the waiting list is only accessed on the loop and thus is not generally
  // protected. However, transfers between the lists are protected by the controller mutex.
  image_node_t node __TA_GUARDED(mtx()) = {
      .link = LIST_INITIAL_CLEARED_VALUE,
      .self = nullptr,
  };

  // Aliases controller_->mtx() for the purpose of thread-safety analysis.
  mtx_t* mtx();

 private:
  // Retires the image and signals |fence|.
  void RetireWithFence(fbl::RefPtr<FenceReference>&& fence);

  image_t info_;
  uint32_t stride_px_;
  Controller* const controller_;

  // z_index is set/read by controller.cpp under its lock
  uint32_t z_index_;

  // Only ever accessed on loop thread, so no synchronization
  fbl::RefPtr<FenceReference> wait_fence_ = nullptr;
  // signal_fence_ is only accessed on the loop. armed_signal_fence_ is accessed
  // under the controller mutex. See comment in ::OnRetire for more details.
  fbl::RefPtr<FenceReference> signal_fence_ = nullptr;
  fbl::RefPtr<FenceReference> armed_signal_fence_ __TA_GUARDED(mtx()) = nullptr;

  // Flag which indicates that the image is currently in some display configuration.
  std::atomic_bool in_use_ = {};
  // Flag indicating that the image is being managed by the display hardware.
  bool presenting_ __TA_GUARDED(mtx()) = false;
  // Flag indicating that the image has started the process of retiring and will be free after
  // the next vsync. This is distinct from presenting_ due to multiplexing the display between
  // multiple clients.
  bool retiring_ __TA_GUARDED(mtx()) = false;

  // flag used to distinguish between an image used for display vs capture
  const bool capture_image_ = false;

  const zx::vmo vmo_;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_IMAGE_H_
