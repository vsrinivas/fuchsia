// Copyright (c) 2022 The Fuchsia Authors
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

#include "igc_driver.h"

#include <zircon/status.h>

#include "src/connectivity/ethernet/drivers/third_party/igc/igc_bind.h"

namespace ethernet {
namespace igc {

constexpr char kNetDevDriverName[] = "igc-netdev";

IgcDevice::IgcDevice(zx_device_t* parent)
    : parent_(parent), netdev_impl_proto_{&this->network_device_impl_protocol_ops_, this} {}

void IgcDevice::Release() { delete this; }

zx_status_t IgcDevice::Init() {
  zx_status_t status = ZX_OK;

  // Add network device.
  static zx_protocol_device_t network_device_ops = {
      .version = DEVICE_OPS_VERSION,
      .release = [](void* ctx) { static_cast<IgcDevice*>(ctx)->Release(); },
  };

  device_add_args_t netDevArgs = {};
  netDevArgs.version = DEVICE_ADD_ARGS_VERSION;
  netDevArgs.name = kNetDevDriverName;
  netDevArgs.ctx = this;
  netDevArgs.ops = &network_device_ops;
  netDevArgs.proto_id = ZX_PROTOCOL_NETWORK_DEVICE_IMPL;
  netDevArgs.proto_ops = netdev_impl_proto_.ops;

  if ((status = device_add(parent_, &netDevArgs, &device_)) != ZX_OK) {
    zxlogf(ERROR, "Failed adding network device: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

// NetworkDevice::Callbacks implementation
zx_status_t IgcDevice::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  netdev_ifc_ = ::ddk::NetworkDeviceIfcProtocolClient(iface);
  return ZX_OK;
}
void IgcDevice::NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie) {
  std::lock_guard lock(started_mutex_);
}
void IgcDevice::NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie) {
  std::lock_guard lock(started_mutex_);
}
void IgcDevice::NetworkDeviceImplGetInfo(device_info_t* out_info) {}
void IgcDevice::NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list, size_t buffers_count) {
  std::lock_guard lock(started_mutex_);
}
void IgcDevice::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list,
                                              size_t buffers_count) {}
void IgcDevice::NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo,
                                            network_device_impl_prepare_vmo_callback callback,
                                            void* cookie) {}
void IgcDevice::NetworkDeviceImplReleaseVmo(uint8_t vmo_id) {}
void IgcDevice::NetworkDeviceImplSetSnoop(bool snoop) {}

}  // namespace igc
}  // namespace ethernet

static zx_status_t igc_bind(void* ctx, zx_device_t* device) {
  std::printf("%s\n", __func__);
  zx_status_t status = ZX_OK;

  auto igc = std::make_unique<ethernet::igc::IgcDevice>(device);
  if ((status = igc->Init()) != ZX_OK) {
    return status;
  }

  // devhost is now responsible for the memory used by igc.
  igc.release();
  return ZX_OK;
}

static zx_driver_ops_t igc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = igc_bind,
};

ZIRCON_DRIVER(igc, igc_driver_ops, "zircon", "0.1");
