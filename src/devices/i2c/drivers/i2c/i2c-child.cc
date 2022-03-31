// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-child.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/sync/completion.h>
#include <threads.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/mutex.h>

#include "src/devices/i2c/drivers/i2c/i2c_bind.h"

namespace i2c {

zx_status_t I2cChild::CreateAndAddDevice(zx_device_t* parent,
                                         const fidl_i2c::wire::I2CChannel& channel,
                                         const fbl::RefPtr<I2cBus>& bus,
                                         async_dispatcher_t* dispatcher) {
  const uint32_t bus_id = channel.has_bus_id() ? channel.bus_id() : 0;
  const uint16_t address = channel.has_address() ? channel.address() : 0;
  const uint32_t i2c_class = channel.has_i2c_class() ? channel.i2c_class() : 0;
  const uint32_t vid = channel.has_vid() ? channel.vid() : 0;
  const uint32_t pid = channel.has_pid() ? channel.pid() : 0;
  const uint32_t did = channel.has_did() ? channel.did() : 0;

  fidl_i2c::wire::I2CChannel local_channel(channel);
  fidl::unstable::OwnedEncodedMessage<fidl_i2c::wire::I2CChannel> metadata(
      fidl::internal::WireFormatVersion::kV2, &local_channel);
  if (!metadata.ok()) {
    zxlogf(ERROR, "Failed to fidl-encode channel: %s", metadata.FormatDescription().data());
    return metadata.status();
  }

  auto metadata_bytes = metadata.GetOutgoingMessage().CopyBytes();
  cpp20::span<const uint8_t> metadata_span(metadata_bytes.data(), metadata_bytes.size());

  if (vid || pid || did) {
    zx_device_prop_t props[] = {
        {BIND_I2C_BUS_ID, 0, bus_id},    {BIND_I2C_ADDRESS, 0, address},
        {BIND_PLATFORM_DEV_VID, 0, vid}, {BIND_PLATFORM_DEV_PID, 0, pid},
        {BIND_PLATFORM_DEV_DID, 0, did}, {BIND_I2C_CLASS, 0, i2c_class},
    };

    return CreateAndAddDevices(parent, address, bus_id, props, metadata_span, bus, dispatcher);
  }

  zx_device_prop_t props[] = {
      {BIND_I2C_BUS_ID, 0, bus_id},
      {BIND_I2C_ADDRESS, 0, address},
      {BIND_I2C_CLASS, 0, i2c_class},
  };

  return CreateAndAddDevices(parent, address, bus_id, props, metadata_span, bus, dispatcher);
}

zx_status_t I2cChild::CreateAndAddDevices(zx_device_t* parent, uint16_t address, uint32_t bus_id,
                                          cpp20::span<const zx_device_prop_t> props,
                                          cpp20::span<const uint8_t> metadata,
                                          const fbl::RefPtr<I2cBus>& bus,
                                          async_dispatcher_t* dispatcher) {
  zx_status_t status =
      I2cBanjoChild::CreateAndAddDevice(parent, address, bus_id, props, metadata, bus);
  if (status != ZX_OK) {
    return status;
  }

  return I2cFidlChild::CreateAndAddDevice(parent, address, bus_id, props, metadata, bus,
                                          dispatcher);
}

zx_status_t I2cBanjoChild::CreateAndAddDevice(zx_device_t* parent, uint16_t address,
                                              uint32_t bus_id,
                                              cpp20::span<const zx_device_prop_t> props,
                                              cpp20::span<const uint8_t> metadata,
                                              const fbl::RefPtr<I2cBus>& bus) {
  fbl::AllocChecker ac;
  std::unique_ptr<I2cBanjoChild> dev(new (&ac) I2cBanjoChild(parent, bus, address));
  if (!ac.check()) {
    zxlogf(ERROR, "Failed to create child device: %s", zx_status_get_string(ZX_ERR_NO_MEMORY));
    return ZX_ERR_NO_MEMORY;
  }

  char name[32];
  snprintf(name, sizeof(name), "i2c-%u-%u", bus_id, address);
  zx_status_t status = dev->DdkAdd(ddk::DeviceAddArgs(name).set_props(props));

  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %s", zx_status_get_string(status));
    return status;
  }

  status = dev->DdkAddMetadata(DEVICE_METADATA_I2C_DEVICE, metadata.data(), metadata.size_bytes());
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAddMetadata failed: %s", zx_status_get_string(status));
  }

  [[maybe_unused]] auto ptr = dev.release();
  return status;
}

zx_status_t I2cFidlChild::CreateAndAddDevice(zx_device_t* parent, uint16_t address, uint32_t bus_id,
                                             cpp20::span<const zx_device_prop_t> props,
                                             cpp20::span<const uint8_t> metadata,
                                             const fbl::RefPtr<I2cBus>& bus,
                                             async_dispatcher_t* dispatcher) {
  fbl::AllocChecker ac;
  std::unique_ptr<I2cFidlChild> dev(new (&ac) I2cFidlChild(parent, bus, address));
  if (!ac.check()) {
    zxlogf(ERROR, "Failed to create child device: %s", zx_status_get_string(ZX_ERR_NO_MEMORY));
    return ZX_ERR_NO_MEMORY;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  dev->outgoing_dir_.emplace(dispatcher);
  dev->outgoing_dir_->svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fidl_i2c::Device2>,
      fbl::MakeRefCounted<fs::Service>(
          [dev = dev.get()](fidl::ServerEnd<fidl_i2c::Device2> request) mutable {
            dev->Bind(std::move(request));
            return ZX_OK;
          }));

  zx_status_t status = dev->outgoing_dir_->Serve(std::move(endpoints->server));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to service the outoing directory: %s", zx_status_get_string(status));
    return status;
  }

  std::array offers = {fidl::DiscoverableProtocolName<fidl_i2c::Device2>};

  char name[32];
  snprintf(name, sizeof(name), "i2c-%u-%u-fidl", bus_id, address);
  status = dev->DdkAdd(ddk::DeviceAddArgs(name)
                           .set_flags(DEVICE_ADD_MUST_ISOLATE)
                           .set_props(props)
                           .set_fidl_protocol_offers(offers)
                           .set_outgoing_dir(endpoints->client.TakeChannel()));

  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %s", zx_status_get_string(status));
    return status;
  }

  status = dev->DdkAddMetadata(DEVICE_METADATA_I2C_DEVICE, metadata.data(), metadata.size_bytes());
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAddMetadata failed: %s", zx_status_get_string(status));
  }

  [[maybe_unused]] auto ptr = dev.release();
  return status;
}

template <typename R, typename C>
void I2cChild::Transfer(R request, C& completer) {
  if (request->segments_is_write.count() < 1) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  auto op_list = std::make_unique<i2c_op_t[]>(request->segments_is_write.count());
  size_t write_cnt = 0;
  size_t read_cnt = 0;
  for (size_t i = 0; i < request->segments_is_write.count(); ++i) {
    if (request->segments_is_write[i]) {
      if (write_cnt >= request->write_segments_data.count()) {
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }
      op_list[i].data_buffer = request->write_segments_data[write_cnt].data();
      op_list[i].data_size = request->write_segments_data[write_cnt].count();
      op_list[i].is_read = false;
      op_list[i].stop = false;
      write_cnt++;
    } else {
      if (read_cnt >= request->read_segments_length.count()) {
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }
      op_list[i].data_buffer = nullptr;  // unused.
      op_list[i].data_size = request->read_segments_length[read_cnt];
      op_list[i].is_read = true;
      op_list[i].stop = false;
      read_cnt++;
    }
  }
  op_list[request->segments_is_write.count() - 1].stop = true;

  if (request->write_segments_data.count() != write_cnt) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (request->read_segments_length.count() != read_cnt) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  struct Ctx {
    sync_completion_t done = {};
    C* completer;
  } ctx;
  ctx.completer = &completer;
  auto callback = [](void* ctx, zx_status_t status, const i2c_op_t* op_list, size_t op_count) {
    auto ctx2 = static_cast<Ctx*>(ctx);
    if (status == ZX_OK) {
      auto reads = std::make_unique<fidl::VectorView<uint8_t>[]>(op_count);
      for (size_t i = 0; i < op_count; ++i) {
        reads[i] = fidl::VectorView<uint8_t>::FromExternal(
            const_cast<uint8_t*>(op_list[i].data_buffer), op_list[i].data_size);
      }
      auto all_reads =
          fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(reads.get(), op_count);
      ctx2->completer->ReplySuccess(std::move(all_reads));
    } else {
      ctx2->completer->ReplyError(status);
    }
    sync_completion_signal(&ctx2->done);
  };
  bus_->Transact(address_, op_list.get(), request->segments_is_write.count(), callback, &ctx);
  sync_completion_wait(&ctx.done, zx::duration::infinite().get());
}

void I2cChild::Transact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                        void* cookie) {
  bus_->Transact(address_, op_list, op_count, callback, cookie);
}

zx_status_t I2cChild::GetMaxTransferSize(size_t* out_size) {
  *out_size = bus_->max_transfer();
  return ZX_OK;
}

}  // namespace i2c
