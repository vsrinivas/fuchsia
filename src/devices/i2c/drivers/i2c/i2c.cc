// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c.h"

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/sync/completion.h>
#include <threads.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/alloc_checker.h>
#include <fbl/mutex.h>

#include "i2c-child.h"
#include "src/devices/i2c/drivers/i2c/i2c_bind.h"

namespace i2c {

void I2cDevice::DdkUnbind(ddk::UnbindTxn txn) {
  for (auto& bus : i2c_buses_) {
    bus->AsyncStop();
  }

  txn.Reply();
}

void I2cDevice::DdkRelease() {
  for (auto& bus : i2c_buses_) {
    bus->WaitForStop();
  }
  delete this;
}

zx_status_t I2cDevice::Create(void* ctx, zx_device_t* parent) {
  i2c_impl_protocol_t i2c;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_I2C_IMPL, &i2c);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<I2cDevice> device(new (&ac) I2cDevice(parent, &i2c));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = device->Init(&i2c);
  if (status != ZX_OK) {
    return status;
  }

  status = device->DdkAdd(ddk::DeviceAddArgs("i2c").set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status != ZX_OK) {
    return status;
  }

  device->AddChildren();

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

zx_status_t I2cDevice::Init(ddk::I2cImplProtocolClient i2c) {
  first_bus_id_ = i2c.GetBusBase();
  uint32_t bus_count = i2c.GetBusCount();
  if (!bus_count) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AllocChecker ac;
  i2c_buses_.reserve(bus_count, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  for (uint32_t i = first_bus_id_; i < first_bus_id_ + bus_count; i++) {
    auto i2c_bus = fbl::MakeRefCountedChecked<I2cBus>(&ac, this->zxdev_, i2c, i);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    auto status = i2c_bus->Start();
    if (status != ZX_OK) {
      return status;
    }

    i2c_buses_.push_back(std::move(i2c_bus));
  }

  return ZX_OK;
}

void I2cDevice::AddChildren() {
  size_t metadata_size;
  auto status = device_get_metadata_size(zxdev(), DEVICE_METADATA_I2C_CHANNELS, &metadata_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata_size failed %d", __func__, status);
    return;
  }

  auto buffer_deleter = std::make_unique<uint8_t[]>(metadata_size);
  auto buffer = buffer_deleter.get();

  size_t actual;
  status =
      device_get_metadata(zxdev(), DEVICE_METADATA_I2C_CHANNELS, buffer, metadata_size, &actual);
  if (status != ZX_OK || actual != metadata_size) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __func__, status);
    return;
  }

  if (metadata_size > UINT32_MAX) {
    zxlogf(ERROR, "metadata too big");
    return;
  }

  fidl::DecodedMessage<fuchsia_hardware_i2c::wire::I2CBusMetadata> decoded(
      fidl::internal::kLLCPPEncodedWireFormatVersion, buffer, static_cast<uint32_t>(metadata_size));
  if (!decoded.ok()) {
    zxlogf(ERROR, "%s: Failed to deserialize metadata.", __func__);
    return;
  }

  fuchsia_hardware_i2c::wire::I2CBusMetadata* metadata = decoded.PrimaryObject();
  if (!metadata->has_channels()) {
    zxlogf(INFO, "%s: no channels supplied.", __func__);
    return;
  }

  zxlogf(INFO, "%s: %zu channels supplied.", __func__, metadata->channels().count());

  for (auto& channel : metadata->channels()) {
    const uint32_t bus_id = channel.has_bus_id() ? channel.bus_id() : 0;
    const uint16_t address = channel.has_address() ? channel.address() : 0;
    const uint32_t i2c_class = channel.has_i2c_class() ? channel.i2c_class() : 0;
    const uint32_t vid = channel.has_vid() ? channel.vid() : 0;
    const uint32_t pid = channel.has_pid() ? channel.pid() : 0;
    const uint32_t did = channel.has_did() ? channel.did() : 0;

    if (bus_id < first_bus_id_ || (bus_id - first_bus_id_) >= i2c_buses_.size()) {
      zxlogf(ERROR, "%s: bus_id %u out of range", __func__, bus_id);
      return;
    }

    const uint32_t bus_index = bus_id - first_bus_id_;

    fbl::AllocChecker ac;
    std::unique_ptr<I2cChild> dev(new (&ac) I2cChild(zxdev(), i2c_buses_[bus_index], address));
    if (!ac.check()) {
      zxlogf(ERROR, "%s: out of memory", __func__);
      return;
    }

    char name[20];
    snprintf(name, sizeof(name), "i2c-%u-%u", bus_id, address);

    auto metadata = fidl::OwnedEncodedMessage<fidl_i2c::wire::I2CChannel>(&channel);
    if (!metadata.ok()) {
      zxlogf(ERROR, "failed to fidl-encode channel: %s", metadata.FormatDescription().data());
      return;
    }
    if (vid || pid || did) {
      zx_device_prop_t props[] = {
          {BIND_I2C_BUS_ID, 0, bus_id},    {BIND_I2C_ADDRESS, 0, address},
          {BIND_PLATFORM_DEV_VID, 0, vid}, {BIND_PLATFORM_DEV_PID, 0, pid},
          {BIND_PLATFORM_DEV_DID, 0, did}, {BIND_I2C_CLASS, 0, i2c_class},
      };

      status = dev->DdkAdd(
          ddk::DeviceAddArgs(name).set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE).set_props(props));
    } else {
      zx_device_prop_t props[] = {
          {BIND_I2C_BUS_ID, 0, bus_id},
          {BIND_I2C_ADDRESS, 0, address},
          {BIND_I2C_CLASS, 0, i2c_class},
      };

      status = dev->DdkAdd(ddk::DeviceAddArgs(name).set_props(props));
    }

    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAdd failed %d", __func__, status);
      return;
    }

    auto bytes = metadata.GetOutgoingMessage().CopyBytes();
    status = dev->DdkAddMetadata(DEVICE_METADATA_I2C_DEVICE, bytes.data(), bytes.size());
    if (status != ZX_OK) {
      zxlogf(ERROR, "DdkAddMetadata failed %d", status);
    }
    // dev is now owned by devmgr.
    __UNUSED auto ptr = dev.release();
  }
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = I2cDevice::Create;
  return ops;
}();

}  // namespace i2c

ZIRCON_DRIVER(i2c, i2c::driver_ops, "zircon", "0.1");
