// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>

#include "src/devices/spi/drivers/spi/spi_bind.h"

namespace spi {

void SpiDevice::DdkUnbind(ddk::UnbindTxn txn) {
  children_.reset();

  txn.Reply();
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

  auto buffer_deleter = std::make_unique<uint8_t[]>(metadata_size);
  auto buffer = buffer_deleter.get();

  size_t actual;
  status =
      device_get_metadata(zxdev(), DEVICE_METADATA_SPI_CHANNELS, buffer, metadata_size, &actual);
  if (status != ZX_OK || actual != metadata_size) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __func__, status);
    return;
  }

  fidl::DecodedMessage<fuchsia_hardware_spi::wire::SpiBusMetadata> decoded(buffer, metadata_size);
  if (!decoded.ok()) {
    zxlogf(ERROR, "%s: Failed to deserialize metadata.", __func__);
    return;
  }

  fuchsia_hardware_spi::wire::SpiBusMetadata* metadata = decoded.PrimaryObject();
  if (!metadata->has_channels()) {
    zxlogf(INFO, "%s: no channels supplied.", __func__);
    return;
  }
  zxlogf(INFO, "%s: %zu channels supplied.", __func__, metadata->channels().count());

  bool has_siblings = metadata->channels().count() > 1;
  for (auto& channel : metadata->channels()) {
    const auto bus_id = channel.has_bus_id() ? channel.bus_id() : 0;

    if (bus_id != bus_id_) {
      continue;
    }

    const auto cs = channel.has_cs() ? channel.cs() : 0;
    const auto vid = channel.has_vid() ? channel.vid() : 0;
    const auto pid = channel.has_pid() ? channel.pid() : 0;
    const auto did = channel.has_did() ? channel.did() : 0;

    fbl::AllocChecker ac;
    auto dev = fbl::MakeRefCountedChecked<SpiChild>(&ac, zxdev(), spi_, cs, this, has_siblings);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: out of memory", __func__);
      return;
    }

    char name[20];
    snprintf(name, sizeof(name), "spi-%u-%u", bus_id, cs);

    if (vid || pid || did) {
      zx_device_prop_t props[] = {
          {BIND_SPI_BUS_ID, 0, bus_id},    {BIND_SPI_CHIP_SELECT, 0, cs},
          {BIND_PLATFORM_DEV_VID, 0, vid}, {BIND_PLATFORM_DEV_PID, 0, pid},
          {BIND_PLATFORM_DEV_DID, 0, did},
      };

      status = dev->DdkAdd(ddk::DeviceAddArgs(name).set_props(props).set_proto_id(ZX_PROTOCOL_SPI));
    } else {
      zx_device_prop_t props[] = {
          {BIND_SPI_BUS_ID, 0, bus_id},
          {BIND_SPI_CHIP_SELECT, 0, cs},
      };

      status = dev->DdkAdd(ddk::DeviceAddArgs(name).set_props(props).set_proto_id(ZX_PROTOCOL_SPI));
    }

    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAdd failed %d", __func__, status);
      return;
    }

    dev->AddRef();  // DdkAdd succeeded -- increment the counter now that the DDK has a reference.

    // save a reference for cleanup
    children_.push_back(dev);
  }
}

void SpiDevice::ConnectServer(zx::channel server, SpiChild* const child) {
  if (!loop_started_.exchange(true)) {
    zx_status_t status;
    if ((status = loop_.StartThread("spi-child-thread")) != ZX_OK) {
      zxlogf(ERROR, "Failed to start async loop: %d", status);
    }
  }

  fidl::BindServer(loop_.dispatcher(), std::move(server), child);
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = SpiDevice::Create;
  return ops;
}();

}  // namespace spi

ZIRCON_DRIVER(spi, spi::driver_ops, "zircon", "0.1");
