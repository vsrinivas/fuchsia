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

  Device(zx_device_t* parent, fdf::Channel ch_to_parent)
      : DeviceType(parent), ch_to_parent_(std::move(ch_to_parent)), unbind_txn_(nullptr) {}

  // TestDeviceChild protocol implementation.
  void GetParentDataOverRuntimeChannel(
      GetParentDataOverRuntimeChannelRequestView request,
      GetParentDataOverRuntimeChannelCompleter::Sync& completer) override;

  void SendRequestSync(fdf::Arena arena, void* req, uint32_t req_size,
                       GetParentDataOverRuntimeChannelCompleter::Sync& completer);
  void SendRequestAsync(fdf::Arena arena, void* req, uint32_t req_size,
                        GetParentDataOverRuntimeChannelCompleter::Sync& completer);

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn) {
    dispatcher_.ShutdownAsync();
    unbind_txn_ = std::move(txn);
  }
  void DdkRelease() { delete this; }

  zx_status_t Init();
  void ShutdownHandler(fdf_dispatcher_t* dispatcher);

 private:
  fdf::Channel ch_to_parent_;
  fdf::Dispatcher dispatcher_;

  ddk::UnbindTxn unbind_txn_;
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

  uint32_t total_size = sizeof(fdf_txid_t) + sizeof(req);
  void* ptr = arena->Allocate(total_size);
  void* data_start = static_cast<uint8_t*>(ptr) + sizeof(fdf_txid_t);
  memcpy(data_start, &req, sizeof(req));

  if (request->sync) {
    SendRequestSync(std::move(*arena), ptr, total_size, completer);
  } else {
    SendRequestAsync(std::move(*arena), ptr, total_size, completer);
  }
}

void Device::SendRequestSync(fdf::Arena arena, void* req, uint32_t req_size,
                             GetParentDataOverRuntimeChannelCompleter::Sync& completer) {
  auto read =
      ch_to_parent_.Call(0, zx::time::infinite(), arena, req, req_size, cpp20::span<zx_handle_t>());
  if (read.is_error()) {
    completer.ReplyError(read.status_value());
    return;
  }

  // Reply to the test's fidl request with the data.
  void* data_start = static_cast<uint8_t*>(read->data) + sizeof(fdf_txid_t);
  size_t data_size = read->num_bytes - sizeof(fdf_txid_t);

  fidl::Arena fidl_arena;
  fidl::VectorView<uint8_t> out_data(fidl_arena, data_size);
  auto* out_ptr = out_data.mutable_data();
  memcpy(out_ptr, data_start, data_size);
  out_data.set_count(data_size);

  completer.ReplySuccess(std::move(out_data));
}

void Device::SendRequestAsync(fdf::Arena arena, void* req, uint32_t req_size,
                              GetParentDataOverRuntimeChannelCompleter::Sync& completer) {
  auto write_status = ch_to_parent_.Write(0, arena, req, req_size, cpp20::span<zx_handle_t>());
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
        void* data_start = static_cast<uint8_t*>(read->data) + sizeof(fdf_txid_t);
        size_t data_size = read->num_bytes - sizeof(fdf_txid_t);

        fidl::Arena fidl_arena;
        fidl::VectorView<uint8_t> out_data(fidl_arena, data_size);
        auto* out_ptr = out_data.mutable_data();
        memcpy(out_ptr, data_start, data_size);
        out_data.set_count(data_size);

        async_completer.ReplySuccess(std::move(out_data));

        delete channel_read;
      });
  zx_status_t status = channel_read->Begin(dispatcher_.get());
  ZX_ASSERT(status == ZX_OK);

  channel_read.release();  // Deleted on callback.
}

void Device::ShutdownHandler(fdf_dispatcher_t* dispatcher) { unbind_txn_.Reply(); }

zx_status_t Device::Init() {
  auto dispatcher = fdf::Dispatcher::Create(0, fit::bind_member(this, &Device::ShutdownHandler));
  if (dispatcher.is_error()) {
    return dispatcher.status_value();
  }
  dispatcher_ = *std::move(dispatcher);
  return ZX_OK;
}

// static
zx_status_t Device::Bind(void* ctx, zx_device_t* device) {
  auto channels = fdf::ChannelPair::Create(0);
  if (channels.is_error()) {
    return channels.status_value();
  }

  auto dev = std::make_unique<Device>(device, std::move(channels->end0));
  zx_status_t status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }
  // Connect to our parent driver.
  status = dev->DdkServiceConnect("test-service", std::move(channels->end1));
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
