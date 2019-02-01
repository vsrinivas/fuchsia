// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_DEVICE_CTX_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_DEVICE_CTX_H_

#include "device_fidl.h"
#include "driver_ctx.h"

#include "amlogic-video.h"

class AmlogicVideo;

// A pointer to an instance of this class is the per-device "ctx".  The purpose
// of this class is to provide a place for device-lifetime stuff to be rooted,
// without itself being any particular aspect of the driver.
//
// TODO(dustingreen): If this device's release() can get called, we'll want to
// sequence the shutdown more carefully/explicitly.  Just destructing an
// instance of this class isn't tested to actually shut down cleanly (yet).
class DeviceCtx {
 public:
  explicit DeviceCtx(DriverCtx* driver);
  ~DeviceCtx();

  zx_status_t Bind(zx_device_t* parent);

  DriverCtx* driver() { return driver_; }

  AmlogicVideo* video() { return video_.get(); }

  DeviceFidl* device_fidl() { return device_fidl_.get(); }

  CodecAdmissionControl* codec_admission_control() {
    return &codec_admission_control_;
  }

 private:
  DriverCtx* driver_ = nullptr;

  //
  // Generic driver/device interfacing:
  //

  // Empty struct to use for proto_ops.
  struct {
    // intentionally empty
  } proto_ops_;
  zx_device_t* device_ = nullptr;

  //
  // Specific device driving:
  //

  std::unique_ptr<AmlogicVideo> video_;

  //
  // FIDL interface handling:
  //

  std::unique_ptr<DeviceFidl> device_fidl_;

  //
  // Codec admission control:
  //

  CodecAdmissionControl codec_admission_control_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_DEVICE_CTX_H_
