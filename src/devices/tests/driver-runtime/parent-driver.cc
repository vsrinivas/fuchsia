// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.runtime.test/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/arena.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>

#include "src/devices/tests/driver-runtime/parent-driver-bind.h"

using fuchsia_device_runtime_test::TestDevice;

class Device;
using DeviceType = ddk::Device<Device, ddk::Unbindable, ddk::ServiceConnectable,
                               ddk::Messageable<TestDevice>::Mixin>;

class Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_TEST> {
 public:
  static zx_status_t Bind(void* ctx, zx_device_t* device);

  Device(zx_device_t* parent, fdf::Dispatcher dispatcher)
      : DeviceType(parent), dispatcher_(std::move(dispatcher)) {}

  // TestDevice protocol implementation.
  void SetTestData(SetTestDataRequestView request, SetTestDataCompleter::Sync& completer) override;

  // Device protocol implementation.
  zx_status_t DdkServiceConnect(const char* service_name, fdf::Channel channel);
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  // Replies to the request in |channel_read|.
  void HandleRuntimeRequest(fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                            zx_status_t status);
  void HandleGetDataRequest(fdf_dispatcher_t* dispatcher, fdf::Arena arena, fdf_txid_t txid);

  fdf::Channel client_;
  fdf::Dispatcher dispatcher_;

  std::unique_ptr<fdf::ChannelRead> channel_read_;

  // Data set by the test using |SetTestData|.
  uint8_t data_[fuchsia_device_runtime_test::wire::kMaxTransferSize];
  size_t data_size_;
};

zx_status_t Device::DdkServiceConnect(const char* service_name, fdf::Channel channel) {
  if (client_.get() != FDF_HANDLE_INVALID) {
    // Only support one client for now.
    return ZX_ERR_NOT_SUPPORTED;
  }
  client_ = std::move(channel);

  channel_read_ = std::make_unique<fdf::ChannelRead>(
      client_.get(), 0 /* options */,
      [this](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        HandleRuntimeRequest(dispatcher, channel_read, status);
      });
  return channel_read_->Begin(dispatcher_.get());
}

void Device::HandleRuntimeRequest(fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                                  zx_status_t status) {
  if (status != ZX_OK) {
    zxlogf(ERROR, "HandleRuntimeRequest got err: %d", status);
    return;
  }

  fdf::UnownedChannel channel(channel_read->channel());
  auto requeue_wait = fit::defer([&]() {
    zx_status_t status = channel_read->Begin(dispatcher_.get());
    if (status != ZX_OK) {
      zxlogf(ERROR, "HandleRuntimeRequest failed wait: %d", status);
    }
  });

  auto read_return = channel->Read(0);
  if (read_return.is_error()) {
    zxlogf(ERROR, "HandleRuntimeRequest read err: %d", read_return.status_value());
    return;
  }

  fuchsia_device_runtime_test::wire::RuntimeRequest req_type;
  ZX_ASSERT(read_return->num_bytes >= sizeof(fdf_txid_t) + sizeof(req_type));

  fdf_txid_t txid = *reinterpret_cast<fdf_txid_t*>(read_return->data);
  void* data_start = static_cast<uint8_t*>(read_return->data) + sizeof(fdf_txid_t);
  memcpy(&req_type, data_start, sizeof(req_type));

  switch (req_type) {
    case fuchsia_device_runtime_test::wire::RuntimeRequest::kGetData:
      HandleGetDataRequest(dispatcher, std::move(read_return->arena), txid);
      return;
    default:
      zxlogf(ERROR, "HandleRuntimeRequest got unknown type: %u\n", req_type);
      return;
  }
}

void Device::HandleGetDataRequest(fdf_dispatcher_t* dispatcher, fdf::Arena arena, fdf_txid_t txid) {
  if (!arena.get()) {
    zxlogf(ERROR, "HandleGetDataRequest was not provided an arena\n");
    return;
  }
  // The reply must start with |txid|.
  void* ptr = arena.Allocate(sizeof(txid) + data_size_);
  memcpy(ptr, &txid, sizeof(txid));
  memcpy(static_cast<uint8_t*>(ptr) + sizeof(fdf_txid_t), data_, data_size_);

  uint32_t total_size = sizeof(txid) + static_cast<uint32_t>(data_size_);

  auto write_status = client_.Write(0, arena, ptr, total_size, cpp20::span<zx_handle_t>());
  if (write_status.is_error()) {
    zxlogf(ERROR, "HandleGetDataRequest got write err: %d", write_status.status_value());
    return;
  }
}

// Sets the test data that will be retrieved by |HandleGetDataRequest|.
void Device::SetTestData(SetTestDataRequestView request, SetTestDataCompleter::Sync& completer) {
  auto ptr = request->in.data();
  data_size_ = request->in.count();
  memcpy(data_, ptr, data_size_);
  completer.ReplySuccess();
}

// static
zx_status_t Device::Bind(void* ctx, zx_device_t* device) {
  auto dispatcher = fdf::Dispatcher::Create(0);
  if (dispatcher.is_error()) {
    return dispatcher.status_value();
  }

  auto dev = std::make_unique<Device>(device, *std::move(dispatcher));
  auto status = dev->DdkAdd("parent");
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

ZIRCON_DRIVER(driver_runtime_test_parent, kDriverOps, "zircon", "0.1");
