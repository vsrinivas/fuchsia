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
  //
  // Also, DeviceFidl::CreateChannelBoundCodecFactory() relies on ability to
  // post work which will run on shared_fidl_thread() before ~DeviceFidl()
  // runs on shared_fidl_thread().
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
  factory->SetErrorHandler([this, raw_factory_ptr = factory.get()] {
    FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
    auto iter = factories_.find(raw_factory_ptr);
    FXL_DCHECK(iter != factories_.end());
    factories_.erase(iter);
  });
  // Any destruction of "this" is also posted over to shared_fidl_thread(), and
  // will run after the work posted here runs.
  //
  // This posting over to shared_fidl_thread() is mainly for the benefit of
  // factories_ only being touched from that thread, and secondarily to avoid
  // taking a dependency on Bind() working from a different thread (both in
  // Bind() and in DeviceFidl code).
  device_->driver()->PostToSharedFidl(
      [this, factory = std::move(factory),
       server_endpoint = std::move(local_server_endpoint)]() mutable {
        FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
        LocalCodecFactory* raw_factory_ptr = factory.get();
        auto insert_result = factories_.insert(
            std::make_pair(raw_factory_ptr, std::move(factory)));
        // insert success
        FXL_DCHECK(insert_result.second);
        insert_result.first->second->Bind(std::move(server_endpoint));
      });
  *client_endpoint = std::move(local_client_endpoint);
}

void DeviceFidl::BindCodecImpl(std::unique_ptr<CodecImpl> codec) {
  FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
  CodecImpl* raw_codec_ptr = codec.get();
  auto insert_result =
      codecs_.insert(std::make_pair(raw_codec_ptr, std::move(codec)));
  // insert success
  FXL_DCHECK(insert_result.second);
  (*insert_result.first).second->BindAsync([this, raw_codec_ptr] {
    printf("BindCodecImpl()'s error handler start\n");
    FXL_DCHECK(thrd_current() == device_->driver()->shared_fidl_thread());
    auto iter = codecs_.find(raw_codec_ptr);
    FXL_DCHECK(iter != codecs_.end());
    codecs_.erase(iter);
    printf("BindCodecImpl()'s error handler end\n");
  });
}
