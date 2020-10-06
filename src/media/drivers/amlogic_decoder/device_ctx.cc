// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_ctx.h"

#include <fuchsia/hardware/mediacodec/c/fidl.h>
#include <lib/sync/completion.h>

#include "amlogic-video.h"
#include "macros.h"

namespace {

const fuchsia_hardware_mediacodec_Device_ops_t kFidlOps = {
    .GetCodecFactory =
        [](void* ctx, zx_handle_t handle) {
          zx::channel request(handle);
          reinterpret_cast<DeviceCtx*>(ctx)->GetCodecFactory(std::move(request));
          return ZX_OK;
        },
};

static zx_status_t amlogic_video_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_mediacodec_Device_dispatch(ctx, txn, msg, &kFidlOps);
}

static void amlogic_release(void* ctx) { delete static_cast<DeviceCtx*>(ctx); }

static zx_protocol_device_t amlogic_video_device_ops = {
    DEVICE_OPS_VERSION, .release = amlogic_release, .message = amlogic_video_message,
    // TODO(jbauman) or TODO(dustingreen): .suspend .resume
};

}  // namespace

DeviceCtx::DeviceCtx(DriverCtx* driver)
    : driver_(driver), codec_admission_control_(driver->shared_fidl_loop()->dispatcher()) {
  video_ = std::make_unique<AmlogicVideo>();
  device_fidl_ = std::make_unique<DeviceFidl>(this);
}

DeviceCtx::~DeviceCtx() {
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
  // This code is only run when we switch between test and production drivers.
  sync_completion_t completion;
  driver_->PostToSharedFidl([this, &completion]() {
    device_fidl_ = nullptr;
    sync_completion_signal(&completion);
  });
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
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

void DeviceCtx::GetCodecFactory(zx::channel request) {
  device_fidl()->ConnectChannelBoundCodecFactory(std::move(request));
}
