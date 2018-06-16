// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_fidl.h"

#include "device_ctx.h"

#include <lib/fxl/logging.h>

#include <threads.h>

DeviceFidl::DeviceFidl(DeviceCtx* device) : device_(device) {
  // TODO(dustingreen): remove this once we're opening the driver and sending it
  // an IOCTL etc.
  //
  // Limited manual self-test for now:
  device_->driver()->PostToSharedFidl([] {
    printf("amlogic_video_decoder: hello from shared_fidl_thread()\n");
  });
}

DeviceFidl::~DeviceFidl() {
  // If we hit this in any useful situation, we'll need to implement this.
  FXL_CHECK(false) << "not implemented";

  // The DeviceCtx should have already moved over to the shared_fidl_thread()
  // for this (if ~DeviceCtx implemented), else it's not safe to ~fidl::Binding.
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
}

void DeviceFidl::CreateChannelBoundCodecFactory(zx::channel* client_endpoint) {
  zx::channel local_client_endpoint;
  zx::channel local_server_endpoint;
  zx_status_t create_status =
      zx::channel::create(0, &local_client_endpoint, &local_server_endpoint);
  if (create_status != ZX_OK) {
    device_->driver()->FatalError("zx::channel::create() failed");
  }
  std::unique_ptr<LocalCodecFactory> factory =
      std::make_unique<LocalCodecFactory>(device_);
  factory->SetErrorHandler([this, factory = factory.get()] {
    std::lock_guard<std::mutex> lock(factories_lock_);
    factories_.erase(factories_.find(factory));
  });
  factory->Bind(std::move(local_server_endpoint));
  {  // scope lock
    std::lock_guard<std::mutex> lock(factories_lock_);
    auto insert_result =
        factories_.insert(std::make_pair(factory.get(), std::move(factory)));
    // insert success
    FXL_DCHECK(insert_result.second);
  }  // ~lock
  *client_endpoint = std::move(local_client_endpoint);
}

void DeviceFidl::BindCodecImpl(std::unique_ptr<CodecImpl> codec) {
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
  codec->SetErrorHandler([this, codec = codec.get()] {
    FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
    codecs_.erase(codecs_.find(codec));
  });
  auto insert_result =
      codecs_.insert(std::make_pair(codec.get(), std::move(codec)));
  // insert success
  FXL_DCHECK(insert_result.second);
  (*insert_result.first).second->Bind();
}
