// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_fidl.h"

#include <lib/media/codec_impl/codec_admission_control.h>
#include <threads.h>

#include "device_ctx.h"

DeviceFidl::DeviceFidl(DeviceCtx* device) : device_(device) {
  // Nothing else to do here.
}

DeviceFidl::~DeviceFidl() {
  // The DeviceCtx should have already moved over to the shared_fidl_thread()
  // for this (if ~DeviceCtx implemented), else it's not safe to ~fidl::Binding.
  //
  // Also, DeviceFidl::ConnectChannelBoundCodecFactory() relies on ability to
  // post work which will run on shared_fidl_thread() before ~DeviceFidl()
  // runs on shared_fidl_thread().
  ZX_DEBUG_ASSERT(thrd_current() == device_->driver()->shared_fidl_thread());
}

void DeviceFidl::ConnectChannelBoundCodecFactory(zx::channel request) {
  auto factory = std::make_unique<LocalCodecFactory>(device_);
  factory->SetErrorHandler([this, raw_factory_ptr = factory.get()] {
    ZX_DEBUG_ASSERT(thrd_current() == device_->driver()->shared_fidl_thread());
    auto iter = factories_.find(raw_factory_ptr);
    ZX_DEBUG_ASSERT(iter != factories_.end());
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
      [this, factory = std::move(factory), server_endpoint = std::move(request)]() mutable {
        ZX_DEBUG_ASSERT(thrd_current() == device_->driver()->shared_fidl_thread());
        LocalCodecFactory* raw_factory_ptr = factory.get();
        auto insert_result = factories_.insert(std::make_pair(raw_factory_ptr, std::move(factory)));
        // insert success
        ZX_DEBUG_ASSERT(insert_result.second);
        insert_result.first->second->Bind(std::move(server_endpoint));
      });
}

void DeviceFidl::BindCodecImpl(std::unique_ptr<CodecImpl> codec) {
  ZX_DEBUG_ASSERT(thrd_current() == device_->driver()->shared_fidl_thread());
  CodecImpl* raw_codec_ptr = codec.get();
  auto insert_result = codecs_.insert(std::make_pair(raw_codec_ptr, std::move(codec)));
  // insert success
  ZX_DEBUG_ASSERT(insert_result.second);
  (*insert_result.first).second->BindAsync([this, raw_codec_ptr] {
    ZX_DEBUG_ASSERT(thrd_current() == device_->driver()->shared_fidl_thread());
    auto iter = codecs_.find(raw_codec_ptr);
    ZX_DEBUG_ASSERT(iter != codecs_.end());
    codecs_.erase(iter);
  });
}
