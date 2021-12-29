// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_IMAGE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_IMAGE_H_

#include <fidl/fuchsia.hardware.display/cpp/wire.h>
#include <fuchsia/hardware/display/controller/c/banjo.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/vmo.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <atomic>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "fence.h"
#include "id-map.h"
#include "util.h"

namespace display {

class Controller;

typedef struct image_node {
  list_node_t link;
  fbl::RefPtr<class Image> self;
} image_node_t;

// An Image is both a reference to an imported pixel buffer (hereafter ImageRef)
// and the state machine (hereafter ImageUse) for tracking its use as part of a config.
//
// ImageUse can be NOT_READY, READY, ACQUIRED, or PRESENTED.
//   NOT_READY: initial state, transitions to READY when wait_event is null or signaled.
//              When returning to NOT_READY via EarlyRetire, the signal_fence will fire.
//   READY: the related ImageRef is ready for use. Controller::ApplyConfig may request a
//          move to ACQUIRED (Acquire) or NOT_READY (EarlyRetire) because another ImageUse
//          was ACQUIRED instead.
//   ACQUIRED: this image will be used on the next display flip. Transitions to PRESENTED
//             when the display hardware reports it in OnVsync.
//   PRESENTED: this image has been observed in OnVsync. Transitions to NOT_READY when
//              the Controller determines that a new ImageUse has been PRESENTED and
//              this one can be retired.
//
// One special transition exists: upon the owning Client's death/disconnection, the
// ImageUse will move from ACQUIRED to NOT_READY.
class Image : public fbl::RefCounted<Image>, public IdMappable<fbl::RefPtr<Image>> {
 public:
  Image(Controller* controller, const image_t& info, zx::vmo vmo, uint32_t stride_px,
        inspect::Node* parent_node, uint32_t client_id);
  Image(Controller* controller, const image_t& info, inspect::Node* parent_node,
        uint32_t client_id);
  ~Image();

  image_t& info() { return info_; }
  uint32_t stride_px() const { return stride_px_; }
  uint32_t client_id() const { return client_id_; }

  // Marks the image as in use.
  bool Acquire();
  // Marks the image as not in use. Should only be called before PrepareFences.
  void DiscardAcquire();
  // Prepare the image for display. It will not be READY until `wait` is
  // signaled, and once the image is no longer displayed `retire` will be signaled.
  void PrepareFences(fbl::RefPtr<FenceReference>&& wait, fbl::RefPtr<FenceReference>&& retire);
  // Called to immediately retire the image if StartPresent hasn't been called yet.
  void EarlyRetire();
  // Called when the image is passed to the display hardware.
  void StartPresent() __TA_REQUIRES(mtx());
  // Called when another image is presented after this one.
  void StartRetire() __TA_REQUIRES(mtx());
  // Called on vsync after StartRetire has been called.
  void OnRetire() __TA_REQUIRES(mtx());

  // Called on all waiting images when any fence fires. Returns true if the image is ready to
  // present.
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

  void set_latest_controller_config_stamp(config_stamp_t stamp) {
    latest_controller_config_stamp_ = stamp;
  }
  config_stamp_t latest_controller_config_stamp() const { return latest_controller_config_stamp_; }

  void set_latest_client_config_stamp(config_stamp_t stamp) { latest_client_config_stamp_ = stamp; }
  config_stamp_t latest_client_config_stamp() const { return latest_client_config_stamp_; }

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
  void InitializeInspect(inspect::Node* parent_node);

  image_t info_;
  uint32_t stride_px_;

  Controller* const controller_;

  // z_index is set/read by controller.cpp under its lock
  uint32_t z_index_;

  // |id_| of the client that created the image.
  const uint32_t client_id_;

  // Stamp of the latest Controller display configuration that uses this image.
  config_stamp_t latest_controller_config_stamp_ = INVALID_CONFIG_STAMP_BANJO;

  // Stamp of the latest display configuration in Client (the DisplayController
  // FIDL service) that uses this image.
  //
  // Note that for an image, it is possible that its |latest_client_config_stamp_|
  // doesn't match the |latest_controller_config_stamp_|. This could happen when
  // a client configuration sets a new layer image but the new image is not
  // ready yet, so the controller has to keep using the old image.
  config_stamp_t latest_client_config_stamp_ = INVALID_CONFIG_STAMP_BANJO;

  // Indicates that the image contents are ready for display.
  // Only ever accessed on loop thread, so no synchronization
  fbl::RefPtr<FenceReference> wait_fence_ = nullptr;

  // retire_fence_ is signaled when an image is no longer used on a display.
  // retire_fence_ is only accessed on the loop. armed_retire_fence_ is accessed
  // under the controller mutex. See comment in ::OnRetire for more details.
  // All retires are performed by the Controller's ApplyConfig/OnDisplayVsync loop.
  fbl::RefPtr<FenceReference> retire_fence_ = nullptr;
  fbl::RefPtr<FenceReference> armed_retire_fence_ __TA_GUARDED(mtx()) = nullptr;

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

  inspect::Node node_;
  inspect::ValueList properties_;
  inspect::BoolProperty presenting_property_;
  inspect::BoolProperty retiring_property_;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_IMAGE_H_
