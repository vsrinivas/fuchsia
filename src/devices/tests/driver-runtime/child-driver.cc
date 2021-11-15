// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.runtime.test/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fidl/llcpp/arena.h>
#include <lib/sync/completion.h>

#include <ddktl/device.h>

#include "src/devices/tests/driver-runtime/child-driver-bind.h"

using fuchsia_device_runtime_test::TestDeviceChild;

class Device;
using DeviceType = ddk::Device<Device, ddk::Unbindable, ddk::Messageable<TestDeviceChild>::Mixin>;

class Device : public DeviceType {
 public:
  static zx_status_t Bind(void* ctx, zx_device_t* device);

  Device(zx_device_t* parent, fdf::Channel ch_to_parent, fdf::Dispatcher dispatcher)
      : DeviceType(parent),
        ch_to_parent_(std::move(ch_to_parent)),
        dispatcher_(std::move(dispatcher)) {}

  // TestDeviceChild protocol implementation.
  void GetParentDataOverRuntimeChannel(
      GetParentDataOverRuntimeChannelRequestView request,
      GetParentDataOverRuntimeChannelCompleter::Sync& completer) override;

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  fdf::Channel ch_to_parent_;
  fdf::Dispatcher dispatcher_;
};

void Device::GetParentDataOverRuntimeChannel(
    GetParentDataOverRuntimeChannelRequestView request,
    GetParentDataOverRuntimeChannelCompleter::Sync& completer) {
  std::string_view tag{""};
  auto arena = fdf::Arena::Create(0, tag);
  if (arena.is_error()) {
    completer.ReplyError(arena.status_value());
    return;
  }

  // Send a request to the parent driver over the runtime channel.
  auto req = fuchsia_device_runtime_test::wire::RuntimeRequest::kGetData;
  void* ptr = arena->Allocate(sizeof(req));
  memcpy(ptr, &req, sizeof(req));

  auto write_status = ch_to_parent_.Write(0, *arena, ptr, sizeof(req), cpp20::span<zx_handle_t>());
  if (write_status.is_error()) {
    completer.ReplyError(write_status.status_value());
    return;
  }

  auto channel_read = std::make_unique<fdf::ChannelRead>(
      ch_to_parent_.get(), 0 /* options */,
      [async_completer = completer.ToAsync()](fdf_dispatcher_t* dispatcher,
                                              fdf::ChannelRead* channel_read,
                                              fdf_status_t status) mutable {
        fdf::UnownedChannel channel(channel_read->channel());
        auto read = channel->Read(0);
        if (read.is_error()) {
          async_completer.ReplyError(read.status_value());
          return;
        }

        // Reply to the test's fidl request with the data.
        fidl::Arena fidl_arena;
        fidl::VectorView<uint8_t> out_data(fidl_arena, read->num_bytes);
        auto* out_ptr = out_data.mutable_data();
        memcpy(out_ptr, read->data, read->num_bytes);
        out_data.set_count(read->num_bytes);

        async_completer.ReplySuccess(std::move(out_data));

        delete channel_read;
      });
  zx_status_t status = channel_read->Begin(dispatcher_.get());
  ZX_ASSERT(status == ZX_OK);

  channel_read.release();  // Deleted on callback.
}

// static
zx_status_t Device::Bind(void* ctx, zx_device_t* device) {
  auto channels = fdf::ChannelPair::Create(0);
  if (channels.is_error()) {
    return channels.status_value();
  }
  // Create a dispatcher to wait on the runtime channel.
  auto dispatcher = fdf::Dispatcher::Create(0);
  if (dispatcher.is_error()) {
    return dispatcher.status_value();
  }

  auto dev = std::make_unique<Device>(device, std::move(channels->end0), *std::move(dispatcher));
  // Connect to our parent driver.
  zx_status_t status = dev->DdkServiceConnect("test-service", std::move(channels->end1));
  if (status != ZX_OK) {
    return status;
  }

  status = dev->DdkAdd("child");
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static zx_driver_ops_t kDriverOps = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Device::Bind;
  return ops;
}();

ZIRCON_DRIVER(driver_runtime_test_child, kDriverOps, "zircon", "0.1");
