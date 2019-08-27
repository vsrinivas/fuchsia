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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim_device.h"

#include <zircon/status.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim.h"

namespace wlan {
namespace brcmfmac {

// static
zx_status_t SimDevice::Create(zx_device_t* parent_device,
                              const std::shared_ptr<simulation::Environment>& env,
                              std::unique_ptr<SimDevice>* device_out) {
  zx_status_t status = ZX_OK;
  zx_device_t* phy_device;
  device_add_args_t add_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "brcmfmac-wlanphy",
      .proto_id = ZX_PROTOCOL_WLANPHY_IMPL,
      // The tests don't access any of the other fields yet
  };

  if ((status = wlan_sim_device_add(parent_device, &add_args, &phy_device)) != ZX_OK) {
    return status;
  }

  *device_out = std::make_unique<SimDevice>(phy_device, env);
  auto bus_register_fn =
      std::bind(&SimDevice::BusRegister, device_out->get(), std::placeholders::_1);
  return (*device_out)
      ->brcmfmac::Device::Init((*device_out)->phy_device_, parent_device, bus_register_fn);
}

zx_status_t SimDevice::BusRegister(brcmf_pub* drvr) {
  zx_status_t status;
  std::unique_ptr<brcmf_bus> bus;

  if ((status = brcmf_sim_register(drvr, &bus, sim_environ_.get())) != ZX_OK) {
    return status;
  }

  brcmf_bus_ = std::move(bus);
  return ZX_OK;
}

SimDevice::~SimDevice() {
  DisableDispatcher();
  if (phy_device_ != nullptr) {
    wlan_sim_device_remove(phy_device_);
  }
  if (brcmf_bus_) {
    brcmf_sim_exit(brcmf_bus_.get());
  }
}

}  // namespace brcmfmac
}  // namespace wlan
