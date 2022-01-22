// Copyright (c) 2019 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_device.h"

#include <zircon/status.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"

namespace wlan {
namespace brcmfmac {
namespace {

constexpr zx_protocol_device_t kSimDeviceOps = {
    .version = DEVICE_OPS_VERSION,
    .release = [](void* ctx) { return static_cast<SimDevice*>(ctx)->DdkRelease(); },
};

}  // namespace

SimDevice::~SimDevice() { ShutdownImpl(); }

void SimDevice::Shutdown() {
  ShutdownImpl();
}

void SimDevice::ShutdownImpl() {
  // Keep a separate implementation for this that's not virtual so that it can be called from the
  // destructor.
  if (brcmf_bus_) {
    brcmf_sim_exit(brcmf_bus_.get());
    brcmf_bus_.reset();
  }
}

// static
zx_status_t SimDevice::Create(zx_device_t* parent_device, simulation::FakeDevMgr* dev_mgr,
                              const std::shared_ptr<simulation::Environment>& env,
                              SimDevice** device_out) {
  zx_status_t status = ZX_OK;
  auto device = std::make_unique<SimDevice>(parent_device, dev_mgr, env);

  std::unique_ptr<DeviceInspect> inspect;
  if ((status = DeviceInspect::Create(env->GetDispatcher(), &inspect)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<brcmf_bus> bus;
  if ((status = brcmf_sim_alloc(device->drvr(), &bus, device->fake_dev_mgr_,
                                device->sim_environ_)) != ZX_OK) {
    return status;
  }

  device->inspect_ = std::move(inspect);
  device->brcmf_bus_ = std::move(bus);

  device_add_args_t add_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "brcmfmac-wlanphy",
      .ctx = device.get(),
      .ops = &kSimDeviceOps,
      .proto_id = ZX_PROTOCOL_WLANPHY_IMPL,
      // The tests don't access any of the other fields yet
  };
  if ((status = dev_mgr->DeviceAdd(parent_device, &add_args, &device->phy_device_)) != ZX_OK) {
    return status;
  }

  // Ownership of the device has been transferred to dev_mgr.
  *device_out = device.release();
  return ZX_OK;
}

zx_status_t SimDevice::BusInit() { return brcmf_sim_register(drvr()); }

async_dispatcher_t* SimDevice::GetDispatcher() { return sim_environ_->GetDispatcher(); }

DeviceInspect* SimDevice::GetInspect() { return inspect_.get(); }

zx_status_t SimDevice::Init() {
  // Not supported.  Manually invoke SimDevice::BusInit() instead.
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SimDevice::DeviceAdd(device_add_args_t* args, zx_device_t** out_device) {
  return fake_dev_mgr_->DeviceAdd(phy_device_, args, out_device);
}

void SimDevice::DeviceAsyncRemove(zx_device_t* dev) { fake_dev_mgr_->DeviceAsyncRemove(dev); }

zx_status_t SimDevice::LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SimDevice::DeviceGetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

brcmf_simdev* SimDevice::GetSim() { return (brcmf_bus_.get())->bus_priv.sim; }

}  // namespace brcmfmac
}  // namespace wlan
