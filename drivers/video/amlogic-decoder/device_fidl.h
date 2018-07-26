// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_DEVICE_FIDL_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_DEVICE_FIDL_H_

#include "codec_impl.h"

#include "local_codec_factory.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/synchronization/thread_annotations.h>

#include <map>

// This class is the per-device FIDL context.
class DeviceFidl {
 public:
  explicit DeviceFidl(DeviceCtx* device);
  ~DeviceFidl();

  // The one IOCTL supported by the driver is to create a CodecFactory channel
  // and return the client endpoint.  This method creates that CodecFactory,
  // associates it with this DeviceCtx, and sets *client_endpoint to the client
  // endpoint.  The LocalCodecFactory instance is destructed if the channel
  // closes or if DeviceCtx is destructed.
  //
  // This method runs on the same thread as the driver's IOCTL handler - the
  // lifetime of the channel is entirely under the control of the driver while
  // this method is running.  The shared_fidl_thread() is used for handling the
  // server end of the channel - nothing related to the channel created by this
  // method runs on shared_fidl_thread() until the caller closes client_endpoint
  // or hands client_endpoint to a client.
  void CreateChannelBoundCodecFactory(zx::channel* client_endpoint);

  // When the LocalCodecFactory creates a CodecImpl to serve a Codec channel
  // associated with this DeviceCtx, it gets handed off to DeviceCtx for
  // lifetime management.  The CodecImpl instance is destructed when the Codec
  // channel closes or the DeviceCtx is destructed.
  //
  // This runs on shared_fidl_thread().
  void BindCodecImpl(std::unique_ptr<CodecImpl> codec);

 private:
  DeviceCtx* device_ = nullptr;

  // We want channel closure to imply LocalCodecFactory instance destruction,
  // and we want DeviceCtx destruction (not presently implemented) to imply all
  // LocalCodecFactory instances get destructed.
  //
  // For consistency with the Codec case we don't use a BindingSet here.  Also
  // in case we need events() in CodecFactory later.
  //
  // A LocalCodecFactory is indirectly responsible for removing itself from this
  // list when its channel closes, via an error handler set by this class on the
  // LocalCodecFactory.  That removal happens on shared_fidl_thread(), as does
  // other LocalCodecFactory work.
  //
  // We allow more than 1 in this set at least to accomodate races if the main
  // CodecFactory restarts.  It's also fine if the main CodecFactory wants to
  // use more than one for convenience and/or to get more coverage on the
  // >1 case here.
  //
  // Only touched from shared_fidl_thread().
  std::map<LocalCodecFactory*, std::unique_ptr<LocalCodecFactory>> factories_;

  // We want channel closure to imply CodecImpl instance destruction, and we
  // want DeviceCtx destruction (not presently implemented) to imply all
  // CodecImpl instances get destructed.
  //
  // The CodecImpl is indirectly responsible for removing itself from this set
  // when the channel closes, via an error handler set by this class on
  // CodecImpl.  The error hander is called on shared_fidl_thread(), along with
  // some of the other non-blocking CodecImpl FIDL handling.
  //
  // We don't use a BindingSet here because we need events().
  //
  // Only touched from shared_fidl_thread().
  std::map<CodecImpl*, std::unique_ptr<CodecImpl>> codecs_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_DEVICE_FIDL_H_
