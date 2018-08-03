// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_ctx.h"

#include "amlogic-video.h"
#include "macros.h"

#include <lib/fxl/logging.h>
#include <zircon/device/media-codec.h>

namespace {

static zx_status_t amlogic_video_ioctl(void* ctx, uint32_t op,
                                       const void* in_buf, size_t in_len,
                                       void* out_buf, size_t out_len,
                                       size_t* out_actual) {
  // The only IOCTL we support is get channel.
  if (op != MEDIA_CODEC_IOCTL_GET_CODEC_FACTORY_CHANNEL) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if ((out_buf == nullptr) || (out_actual == nullptr) ||
      (out_len != sizeof(zx_handle_t))) {
    return ZX_ERR_INVALID_ARGS;
  }

  DeviceCtx* device = reinterpret_cast<DeviceCtx*>(ctx);

  zx::channel codec_factory_client_endpoint;
  device->device_fidl()->CreateChannelBoundCodecFactory(
      &codec_factory_client_endpoint);

  *(reinterpret_cast<zx_handle_t*>(out_buf)) =
      codec_factory_client_endpoint.release();
  *out_actual = sizeof(zx_handle_t);

  return ZX_OK;
}

static zx_protocol_device_t amlogic_video_device_ops = {
    DEVICE_OPS_VERSION, .ioctl = amlogic_video_ioctl,
    // TODO(jbauman) or TODO(dustingreen): .suspend .resume, maybe .release if
    // it would ever be run.  Currently ~AmlogicVideo code sets lower power, but
    // ~AmlogicVideo doesn't run yet.
};

}  // namespace

DeviceCtx::DeviceCtx(DriverCtx* driver)
    : driver_(driver), codec_admission_control_(this) {
  video_ = std::make_unique<AmlogicVideo>();
  device_fidl_ = std::make_unique<DeviceFidl>(this);
}

DeviceCtx::~DeviceCtx() {
  // TODO(dustingreen): Depending on whether device .release() can get called on
  // this deivce, we'll likely need to sequence the shutdown more explicitly.
  // This destruction order seems like a reasonable starting point, but is not
  // tested:
  //
  // ~device_fidl_
  // ~video_
  //
  // There are two ways to destroy a fidl::Binding safely:
  //   * Switch to FIDL thread before Unbind() or ~Binding.
  //   * async::Loop Quit() + JoinThreads() before Unbind() or ~Binding
  //
  // For now this code (if implementation needed) will choose the first option
  // by destructing DeviceFidl on the FIDL thread. The current way forces this
  // thread to wait in this destructor until the shared_fidl_thread() is done
  // processing ~DeviceFidl, which means we require that ~DeviceCtx is not
  // itself running on the shared_fidl_thread() (or we could check which thread
  // here, if we really need to).
  //
  // For now, it's not clear that we actually need to implement this destructor
  // however, since this device is very likely to have a dedicated devhost and
  // may not .release() the device - and even if it does .release() the device
  // there is no clear need for the cleanup described above to actually be
  // implemented.

  // TODO(dustingreen): Implement this destructor iff it's actually used/called.
  FXL_LOG(FATAL) << "not implemented";
}

zx_status_t DeviceCtx::Bind(zx_device_t* parent) {
  device_add_args_t vc_video_args = {};
  vc_video_args.version = DEVICE_ADD_ARGS_VERSION;
  vc_video_args.name = "amlogic_video";
  vc_video_args.ctx = this;
  vc_video_args.ops = &amlogic_video_device_ops;

  // ZX_PROTOCOL_MEDIA_CODEC causes /dev/class/media-codec to get created, and
  // flags support for MEDIA_CODEC_IOCTL_GET_CODEC_FACTORY_CHANNEL.  The
  // proto_ops_ is empty but has a non-null address, so we don't break the
  // invariant that devices with a protocol have non-null proto_ops.
  vc_video_args.proto_id = ZX_PROTOCOL_MEDIA_CODEC;
  vc_video_args.proto_ops = &proto_ops_;

  zx_status_t status = device_add(parent, &vc_video_args, &device_);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to bind device");
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}
