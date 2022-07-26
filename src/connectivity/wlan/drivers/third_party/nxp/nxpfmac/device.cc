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

#include "device.h"

#include <zircon/status.h>
#include <zircon/errors.h>
#include <ddktl/fidl.h>
#include <ddktl/init-txn.h>
#include <wlan/common/ieee80211.h>

namespace wlan {
namespace nxpfmac {

Device::Device(zx_device_t* parent) : DeviceType(parent) {}

Device::~Device() = default;

void Device::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  zxlogf(ERROR, "nxpfmac: Not Supported");
  txn.Reply(status);
}

void Device::DdkRelease() { delete this; }

void Device::DdkSuspend(ddk::SuspendTxn txn) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  txn.Reply(ZX_ERR_NOT_SUPPORTED, txn.requested_state());
}

zx_status_t Device::WlanphyImplGetSupportedMacRoles(
    wlan_mac_role_t out_supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
    uint8_t* out_supported_mac_roles_count) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                           uint16_t* out_iface_id) {
  *out_iface_id = 0;
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanphyImplDestroyIface(uint16_t iface_id) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanphyImplSetCountry(const wlanphy_country_t* country) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanphyImplClearCountry() {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanphyImplGetCountry(wlanphy_country_t* out_country) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanphyImplSetPsMode(const wlanphy_ps_mode_t* ps_mode) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanphyImplGetPsMode(wlanphy_ps_mode_t* out_ps_mode) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

void Device::DestroyAllIfaces(void) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return;
}

zx_status_t Device::NetDevInit() {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

void Device::NetDevRelease() {
  zxlogf(ERROR, "nxpfmac: Not Supported");
}

void Device::NetDevStart(wlan::drivers::components::NetworkDevice::Callbacks::StartTxn txn) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  txn.Reply(ZX_ERR_NOT_SUPPORTED);
}

void Device::NetDevStop(wlan::drivers::components::NetworkDevice::Callbacks::StopTxn txn) {
  // Flush all buffers in response to this call. They are no longer valid for use.
  zxlogf(ERROR, "nxpfmac: Not Supported");
  txn.Reply();
}

void Device::NetDevGetInfo(device_info_t* out_info) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
}

void Device::NetDevQueueTx(cpp20::span<wlan::drivers::components::Frame> frames) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
}

void Device::NetDevQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count,
                                uint8_t* vmo_addrs[]) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
}

zx_status_t Device::NetDevPrepareVmo(uint8_t vmo_id, zx::vmo vmo, uint8_t* mapped_address,
                                     size_t mapped_size) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
  return ZX_ERR_NOT_SUPPORTED;
}

void Device::NetDevReleaseVmo(uint8_t vmo_id) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
}

void Device::NetDevSetSnoopEnabled(bool snoop) {
  zxlogf(ERROR, "nxpfmac: Not Supported");
}
}  // namespace nxpfmac
}  // namespace wlan
