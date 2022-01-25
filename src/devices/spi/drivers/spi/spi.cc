// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>

#include <fbl/auto_lock.h>

#include "src/devices/spi/drivers/spi/spi_bind.h"

namespace spi {

void SpiDevice::Shutdown() {
  fbl::AutoLock lock(&lock_);
  if (!shutdown_) {
    shutdown_ = true;
    // Stop the loop so that all unbind hooks run, and all child references are released.
    loop_.Shutdown();
    children_.reset();
  }
}

void SpiDevice::DdkUnbind(ddk::UnbindTxn txn) {
  Shutdown();
  txn.Reply();
}

void SpiDevice::DdkRelease() {
  Shutdown();
  delete this;
}

zx_status_t SpiDevice::Create(void* ctx, zx_device_t* parent) {
  ddk::SpiImplProtocolClient spi(parent);
  if (!spi.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  uint32_t bus_id;
  size_t actual;
  zx_status_t status =
      device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &bus_id, sizeof bus_id, &actual);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<SpiDevice> device(new (&ac) SpiDevice(parent, bus_id));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = device->DdkAdd(ddk::DeviceAddArgs("spi").set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status != ZX_OK) {
    return status;
  }

  device->AddChildren(spi);

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

void SpiDevice::AddChildren(const ddk::SpiImplProtocolClient& spi) {
  auto decoded = ddk::GetEncodedMetadata<fuchsia_hardware_spi::wire::SpiBusMetadata>(
      zxdev(), DEVICE_METADATA_SPI_CHANNELS);
  if (!decoded.is_ok()) {
    return;
  }

  fuchsia_hardware_spi::wire::SpiBusMetadata* metadata = decoded->PrimaryObject();
  if (!metadata->has_channels()) {
    zxlogf(INFO, "No channels supplied.");
    return;
  }
  zxlogf(INFO, "%zu channels supplied.", metadata->channels().count());

  fbl::AutoLock lock(&lock_);

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
    auto dev = fbl::MakeRefCountedChecked<SpiChild>(&ac, zxdev(), spi, cs, this, has_siblings);
    if (!ac.check()) {
      zxlogf(ERROR, "Out of memory");
      return;
    }

    char name[20];
    snprintf(name, sizeof(name), "spi-%u-%u", bus_id, cs);
    zx_status_t status;
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
      zxlogf(ERROR, "DdkAdd failed %d", status);
      return;
    }

    dev->AddRef();  // DdkAdd succeeded -- increment the counter now that the DDK has a reference.

    // save a reference for cleanup
    children_.push_back(dev);
  }
}

void SpiDevice::ConnectServer(zx::channel server, fbl::RefPtr<SpiChild> child) {
  fbl::AutoLock lock(&lock_);
  if (shutdown_) {
    fidl::ServerEnd<fuchsia_hardware_spi::Device>(std::move(server)).Close(ZX_ERR_ALREADY_BOUND);
    return;
  }

  if (!loop_started_) {
    zx_status_t status;
    if ((status = loop_.StartThread("spi-child-thread")) != ZX_OK) {
      zxlogf(ERROR, "Failed to start async loop: %d", status);
    } else {
      loop_started_ = true;
    }
  }

  // Make sure that the child reference counter doesn't get decreased when it goes out of scope
  // here. The dispatcher now holds a pointer to the child, so the child can't be freed until after
  // the callback runs.
  fidl::BindServer(loop_.dispatcher(), std::move(server), fbl::ExportToRawPtr(&child),
                   [](SpiChild* ref_counted_child, fidl::UnbindInfo info,
                      fidl::ServerEnd<fuchsia_hardware_spi::Device> server) {
                     fbl::RefPtr<SpiChild> child_ref_ptr = fbl::ImportFromRawPtr(ref_counted_child);
                     child_ref_ptr->OnUnbound();
                   });
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = SpiDevice::Create;
  return ops;
}();

}  // namespace spi

ZIRCON_DRIVER(spi, spi::driver_ops, "zircon", "0.1");
