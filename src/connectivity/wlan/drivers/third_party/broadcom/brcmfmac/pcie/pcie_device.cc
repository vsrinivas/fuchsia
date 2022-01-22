// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_device.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <ddktl/init-txn.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/inspect/device_inspect.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_proto.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_bus.h"

namespace wlan {
namespace brcmfmac {

PcieDevice::PcieDevice(zx_device_t* parent) : Device(parent) {}

PcieDevice::~PcieDevice() = default;

void PcieDevice::Shutdown() {
  if (async_loop_) {
    // Explicitly destroy the async loop before further shutdown to prevent asynchronous tasks
    // from using resources as they are being deallocated.
    async_loop_.reset();
  }
  brcmf_pub* pub = drvr();
  if (pub && pub->bus_if && pub->bus_if->state == BRCMF_BUS_UP) {
    brcmf_detach(pub);
  }
}

// static
zx_status_t PcieDevice::Create(zx_device_t* parent_device) {
  zx_status_t status = ZX_OK;

  auto async_loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  if ((status = async_loop->StartThread("brcmfmac-worker", nullptr)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<DeviceInspect> inspect;
  if ((status = DeviceInspect::Create(async_loop->dispatcher(), &inspect)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<PcieDevice> device(new PcieDevice(parent_device));
  device->async_loop_ = std::move(async_loop);
  device->inspect_ = std::move(inspect);

  if ((status = device->DdkAdd(
           ddk::DeviceAddArgs("brcmfmac-wlanphy")
               .set_inspect_vmo(device->inspect_->inspector().DuplicateVmo()))) != ZX_OK) {
    return status;
  }
  device.release();  // This now has its lifecycle managed by the devhost.

  // Further initialization is performed in the PcieDevice::Init() DDK hook, invoked by the devhost.
  return ZX_OK;
}

async_dispatcher_t* PcieDevice::GetDispatcher() { return async_loop_->dispatcher(); }

DeviceInspect* PcieDevice::GetInspect() { return inspect_.get(); }

zx_status_t PcieDevice::Init() {
  zx_status_t status = ZX_OK;

  std::unique_ptr<PcieBus> pcie_bus;
  if ((status = PcieBus::Create(this, &pcie_bus)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<MsgbufProto> msgbuf_proto;
  if ((status = MsgbufProto::Create(this, pcie_bus->GetDmaBufferProvider(),
                                    pcie_bus->GetDmaRingProvider(),
                                    pcie_bus->GetInterruptProvider(), &msgbuf_proto)) != ZX_OK) {
    return status;
  }

  pcie_bus_ = std::move(pcie_bus);
  msgbuf_proto_ = std::move(msgbuf_proto);

  if ((status = brcmf_attach(drvr())) != ZX_OK) {
    BRCMF_ERR("Failed to attach: %s", zx_status_get_string(status));
    return status;
  }

  if ((status = brcmf_bus_started(drvr(), false)) != ZX_OK) {
    BRCMF_ERR("Failed to start bus: %s", zx_status_get_string(status));
    return status;
  }

  // TODO(sheu): make the device visible once higher-level functionality is present.
  // return ZX_OK;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PcieDevice::DeviceAdd(device_add_args_t* args, zx_device_t** out_device) {
  return device_add(parent(), args, out_device);
}

void PcieDevice::DeviceAsyncRemove(zx_device_t* dev) { device_async_remove(dev); }

zx_status_t PcieDevice::LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) {
  return load_firmware(zxdev(), path, fw, size);
}

zx_status_t PcieDevice::DeviceGetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) {
  return device_get_metadata(zxdev(), type, buf, buflen, actual);
}

}  // namespace brcmfmac
}  // namespace wlan
