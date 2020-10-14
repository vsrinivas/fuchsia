// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wlan_phy.h"

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <wlan/common/phy.h>

#include "bus.h"
#include "src/connectivity/wlan/drivers/realtek/rtl88xx/rtl88xx-bind.h"

namespace wlan {
namespace rtl88xx {
namespace {

constexpr char kWlanPhyDeviceName[] = "rtl88xx-wlanphy";
constexpr uint16_t kWlanPhyIfaceId = 1;

}  // namespace

WlanPhy::WlanPhy() : zx_device_(nullptr), wlan_mac_(nullptr) {}

WlanPhy::~WlanPhy() {
  ZX_DEBUG_ASSERT_MSG(zx_device_ == nullptr, "rtl88xx: WlanPhy did not unbind zx_device_\n");
  ZX_DEBUG_ASSERT_MSG(wlan_mac_ == nullptr, "rtl88xx: WlanPhy leaked wlan_mac_\n");
  zx_device_ = nullptr;
  wlan_mac_ = nullptr;
}

// static
zx_status_t WlanPhy::Create(zx_device_t* bus_device) {
  std::unique_ptr<WlanPhy> wlan_phy(new WlanPhy());
  zx_status_t status = ZX_OK;

  std::unique_ptr<Bus> bus;
  status = Bus::Create(bus_device, &bus);
  if (status != ZX_OK) {
    return status;
  }

  status = Device::Create(std::move(bus), &wlan_phy->device_);
  if (status != ZX_OK) {
    return status;
  }

  static zx_protocol_device_t device_ops = {
      .version = DEVICE_OPS_VERSION,
      .unbind = [](void* ctx) { reinterpret_cast<WlanPhy*>(ctx)->Unbind(); },
      .release = [](void* ctx) { reinterpret_cast<WlanPhy*>(ctx)->Release(); },
  };

  static wlanphy_impl_protocol_ops_t wlanphy_impl_ops = {
      .query = [](void* ctx, wlanphy_impl_info_t* info) -> zx_status_t {
        return reinterpret_cast<WlanPhy*>(ctx)->Query(info);
      },
      .create_iface = [](void* ctx, const wlanphy_impl_create_iface_req_t* req,
                         uint16_t* out_iface_id) -> zx_status_t {
        return reinterpret_cast<WlanPhy*>(ctx)->CreateIface(req, out_iface_id);
      },
      .destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t {
        return reinterpret_cast<WlanPhy*>(ctx)->DestroyIface(id);
      },
      .set_country = [](void* ctx, const wlanphy_country_t* country) -> zx_status_t {
        return reinterpret_cast<WlanPhy*>(ctx)->SetCountry(country);
      },
      .get_country = [](void* ctx, wlanphy_country_t* out_country) -> zx_status_t {
        return reinterpret_cast<WlanPhy*>(ctx)->GetCountry(out_country);
      },
  };

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = kWlanPhyDeviceName,
      .ctx = static_cast<void*>(wlan_phy.get()),
      .ops = &device_ops,
      .proto_id = ZX_PROTOCOL_WLANPHY_IMPL,
      .proto_ops = static_cast<void*>(&wlanphy_impl_ops),
  };
  status = device_add(bus_device, &args, &wlan_phy->zx_device_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "rtl88xx: WlanPhy failed to create zx_device_: %s", zx_status_get_string(status));
    return status;
  }

  // The lifetime of this wlan_phy instance is now managed by the devhost.
  wlan_phy.release();
  return ZX_OK;
}

void WlanPhy::Unbind() {
  zx_device_t* const to_remove = zx_device_;
  zx_device_ = nullptr;

  // Call device_unbind_reply() last, as it may call Release().
  device_unbind_reply(to_remove);
}

void WlanPhy::Release() { delete this; }

zx_status_t WlanPhy::Query(wlanphy_impl_info_t* info) {
  if (wlan_mac_ == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }
  return wlan_mac_->Query(info);
}

zx_status_t WlanPhy::CreateIface(const wlanphy_impl_create_iface_req_t* req,
                                 uint16_t* out_iface_id) {
  if (wlan_mac_ != nullptr) {
    return ZX_ERR_ALREADY_BOUND;
  }
  WlanMac* wlan_mac = nullptr;
  const zx_status_t status = device_->CreateWlanMac(zx_device_, &wlan_mac);
  if (status != ZX_OK) {
    return status;
  }

  wlan_mac_ = wlan_mac;
  *out_iface_id = kWlanPhyIfaceId;
  return ZX_OK;
}

zx_status_t WlanPhy::DestroyIface(uint16_t id) {
  if (wlan_mac_ == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }
  if (id != kWlanPhyIfaceId) {
    return ZX_ERR_NOT_FOUND;
  }
  const zx_status_t status = wlan_mac_->Destroy();
  if (status != ZX_OK) {
    return status;
  }

  // The lifetime of `wlan_mac_` is managed by the devhost, so we do not delete it here.
  wlan_mac_ = nullptr;
  return ZX_OK;
}

zx_status_t WlanPhy::SetCountry(const wlanphy_country_t* country) {
  if (country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  zxlogf(ERROR, "rtl88xx: SetCountry to [%s] not implemented",
         wlan::common::Alpha2ToStr(country->alpha2).c_str());
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t WlanPhy::GetCountry(wlanphy_country_t* out_country) {
  if (out_country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  zxlogf(ERROR, "rtl88xx: GetCountry not implemented");
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace rtl88xx
}  // namespace wlan

zx_status_t rtl88xx_bind_wlan_phy(void* ctx, zx_device_t* device) {
  // Forward to the actual implementation in C++.
  return ::wlan::rtl88xx::WlanPhy::Create(device);
}

static constexpr zx_driver_ops_t rtl88xx_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = &rtl88xx_bind_wlan_phy;
  return ops;
}();

ZIRCON_DRIVER(rtl88xx, rtl88xx_driver_ops, "zircon", "0.1");
