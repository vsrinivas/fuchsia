// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_DEVICE_CTX_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_DEVICE_CTX_H_

#include <fuchsia/hardware/mediacodec/llcpp/fidl.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

#include "amlogic-video.h"
#include "device_fidl.h"
#include "driver_ctx.h"

class AmlogicVideo;
class DeviceCtx;

using DdkDeviceType = ddk::Device<DeviceCtx, ddk::Messageable>;

// A pointer to an instance of this class is the per-device "ctx".  The purpose
// of this class is to provide a place for device-lifetime stuff to be rooted,
// without itself being any particular aspect of the driver.
//
// TODO(dustingreen): If this device's release() can get called, we'll want to
// sequence the shutdown more carefully/explicitly.  Just destructing an
// instance of this class isn't tested to actually shut down cleanly (yet).
class DeviceCtx : public fuchsia_hardware_mediacodec::Device::Interface,
                  public DdkDeviceType,
                  public ddk::EmptyProtocol<ZX_PROTOCOL_MEDIA_CODEC> {
 public:
  DeviceCtx(DriverCtx* driver, zx_device_t* parent);
  ~DeviceCtx();

  zx_status_t Bind();

  DriverCtx* driver() { return driver_; }

  AmlogicVideo* video() { return video_.get(); }

  DeviceFidl* device_fidl() { return device_fidl_.get(); }

  CodecAdmissionControl* codec_admission_control() { return &codec_admission_control_; }

  CodecMetrics& metrics();

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() { delete this; }

  // mediacodec impl.
  void GetCodecFactory(zx::channel request, GetCodecFactoryCompleter::Sync& completer) override;
  void SetAuxServiceDirectory(fidl::ClientEnd<fuchsia_io::Directory> directory,
                              SetAuxServiceDirectoryCompleter::Sync& completer) override;

 private:
  DriverCtx* driver_ = nullptr;

  //
  // Generic driver/device interfacing:
  //

  // Specific device driving:
  std::unique_ptr<AmlogicVideo> video_;

  // FIDL interface handling:
  std::unique_ptr<DeviceFidl> device_fidl_;

  // Codec admission control:
  CodecAdmissionControl codec_admission_control_;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_DEVICE_CTX_H_
