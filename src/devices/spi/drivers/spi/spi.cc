// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>

namespace spi {

void SpiDevice::DdkUnbindDeprecated() {
  for (size_t i = 0; i < children_.size(); i++) {
    auto child = children_[i].get();
    if (child->zxdev()) {
      child->DdkRemoveDeprecated();
    }
  }
  children_.reset();

  DdkRemoveDeprecated();
}

void SpiDevice::DdkRelease() { delete this; }

zx_status_t SpiDevice::Create(void* ctx, zx_device_t* parent) {
  spi_impl_protocol_t spi;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_SPI_IMPL, &spi);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t bus_id;
  size_t actual;
  status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &bus_id, sizeof bus_id, &actual);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<SpiDevice> device(new (&ac) SpiDevice(parent, &spi, bus_id));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = device->DdkAdd("spi");
  if (status != ZX_OK) {
    return status;
  }

  device->AddChildren();

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

void SpiDevice::AddChildren() {
  size_t metadata_size;
  auto status = device_get_metadata_size(zxdev(), DEVICE_METADATA_SPI_CHANNELS, &metadata_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata_size failed %d", __func__, status);
    return;
  }
  auto channel_count = metadata_size / sizeof(spi_channel_t);

  fbl::AllocChecker ac;
  std::unique_ptr<spi_channel_t[]> channels(new (&ac) spi_channel_t[channel_count]);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: out of memory", __func__);
    return;
  }

  size_t actual;
  status = device_get_metadata(zxdev(), DEVICE_METADATA_SPI_CHANNELS, channels.get(), metadata_size,
                               &actual);
  if (status != ZX_OK || actual != metadata_size) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __func__, status);
    return;
  }

  for (uint32_t i = 0; i < channel_count; i++) {
    const auto& channel = channels[i];
    const auto bus_id = channel.bus_id;

    if (channel.bus_id != bus_id_) {
      continue;
    }

    const auto cs = channel.cs;
    const auto vid = channel.vid;
    const auto pid = channel.pid;
    const auto did = channel.did;

    fbl::AllocChecker ac;
    auto dev = fbl::MakeRefCountedChecked<SpiChild>(&ac, zxdev(), spi_, &channel);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: out of memory", __func__);
      return;
    }

    char name[20];
    snprintf(name, sizeof(name), "spi-%u-%u", bus_id, cs);

    if (vid || pid || did) {
      zx_device_prop_t props[] = {
          {BIND_SPI_BUS_ID, 0, bus_id},
          {BIND_SPI_CHIP_SELECT, 0, cs},
          {BIND_TOPO_SPI, 0, BIND_TOPO_SPI_PACK(bus_id, cs)},
          {BIND_PLATFORM_DEV_VID, 0, vid},
          {BIND_PLATFORM_DEV_PID, 0, pid},
          {BIND_PLATFORM_DEV_DID, 0, did},
      };

      status = dev->DdkAdd(name, 0, props, countof(props), ZX_PROTOCOL_SPI);
    } else {
      zx_device_prop_t props[] = {
          {BIND_SPI_BUS_ID, 0, bus_id},
          {BIND_SPI_CHIP_SELECT, 0, cs},
          {BIND_TOPO_SPI, 0, BIND_TOPO_SPI_PACK(bus_id, cs)},
      };

      status = dev->DdkAdd(name, 0, props, countof(props), ZX_PROTOCOL_SPI);
    }

    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAdd failed %d", __func__, status);
      return;
    }

    // save a reference for cleanup
    children_.push_back(dev);
  }
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = SpiDevice::Create;
  return ops;
}();

}  // namespace spi

ZIRCON_DRIVER_BEGIN(spi, spi::driver_ops, "zircon", "0.1", 1)
BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SPI_IMPL), ZIRCON_DRIVER_END(i2c)
