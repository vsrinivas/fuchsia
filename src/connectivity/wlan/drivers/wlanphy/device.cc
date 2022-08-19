// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/100036): Deprecate this file when all the drivers that wlanphy driver
// binds to gets migrated to DFv2.
#include "device.h"

#include <fidl/fuchsia.wlan.device/cpp/wire.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <lib/ddk/device.h>
#include <net/ethernet.h>
#include <zircon/status.h>

#include <algorithm>
#include <iterator>

#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/common/element.h>
#include <wlan/common/phy.h>

#include "ddktl/fidl.h"
#include "debug.h"
#include "driver.h"

namespace wlanphy {

class DeviceConnector : public fidl::WireServer<fuchsia_wlan_device::Connector> {
 public:
  explicit DeviceConnector(Device* device) : device_(device) {}
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& _completer) override {
    device_->Connect(std::move(request->request));
  }

 private:
  Device* device_;
};

Device::Device(zx_device_t* device, wlanphy_impl_protocol_t wlanphy_impl_proto)
    : parent_(device), wlanphy_impl_(wlanphy_impl_proto), server_dispatcher_(wlanphy_async_t()) {
  ltrace_fn();
  // Assert minimum required functionality from the wlanphy_impl driver
  ZX_ASSERT(wlanphy_impl_.ops != nullptr && wlanphy_impl_.ops->get_supported_mac_roles != nullptr &&
            wlanphy_impl_.ops->create_iface != nullptr &&
            wlanphy_impl_.ops->destroy_iface != nullptr &&
            wlanphy_impl_.ops->set_country != nullptr && wlanphy_impl_.ops->get_country != nullptr);
}

Device::~Device() { ltrace_fn(); }

#define DEV(c) (static_cast<Device*>(c))
static zx_protocol_device_t wlanphy_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_incoming_msg_t* msg,
                  fidl_txn_t* txn) { return DEV(ctx)->Message(msg, txn); },
};
#undef DEV

zx_status_t Device::Connect(fidl::ServerEnd<fuchsia_wlan_device::Phy> server_end) {
  ltrace_fn();
  fidl::BindServer<fidl::WireServer<fuchsia_wlan_device::Phy>>(server_dispatcher_,
                                                               std::move(server_end), this);
  return ZX_OK;
}

zx_status_t Device::Bind() {
  ltrace_fn();
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlanphy";
  args.ctx = this;
  args.ops = &wlanphy_device_ops;
  args.proto_id = ZX_PROTOCOL_WLANPHY;

  zx_status_t status = device_add(parent_, &args, &zxdev_);

  if (status != ZX_OK) {
    lerror("could not add device: %s\n", zx_status_get_string(status));
  }

  return status;
}

zx_status_t Device::Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  DeviceConnector connector(this);

  fidl::WireDispatch<fuchsia_wlan_device::Connector>(
      &connector, fidl::IncomingHeaderAndMessage::FromEncodedCMessage(msg), &transaction);
  return transaction.Status();
}

void Device::Release() {
  ltrace_fn();
  delete this;
}

void Device::Unbind() {
  ltrace_fn();
  wlanphy_destroy_loop();
  device_unbind_reply(zxdev_);
}

void Device::GetSupportedMacRoles(GetSupportedMacRolesRequestView request,
                                  GetSupportedMacRolesCompleter::Sync& completer) {
  ltrace_fn();
  std::vector<fuchsia_wlan_common::WlanMacRole> out_supported_mac_roles;

  wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common::wire::kMaxSupportedMacRoles];
  uint8_t supported_mac_roles_count;

  zx_status_t status = wlanphy_impl_.ops->get_supported_mac_roles(
      wlanphy_impl_.ctx, supported_mac_roles_list, &supported_mac_roles_count);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  out_supported_mac_roles.reserve(supported_mac_roles_count);

  for (size_t i = 0; i < supported_mac_roles_count; i++) {
    wlan_mac_role_t mac_role = supported_mac_roles_list[i];
    switch (mac_role) {
      case WLAN_MAC_ROLE_CLIENT:
        out_supported_mac_roles.push_back(fuchsia_wlan_common::WlanMacRole::kClient);
        break;
      case WLAN_MAC_ROLE_AP:
        out_supported_mac_roles.push_back(fuchsia_wlan_common::WlanMacRole::kAp);
        break;
      case WLAN_MAC_ROLE_MESH:
        out_supported_mac_roles.push_back(fuchsia_wlan_common::WlanMacRole::kMesh);
        break;
      default:
        lwarn("encountered unknown MAC role: %u", mac_role);
    }
  }
  auto reply_data =
      fidl::VectorView<fuchsia_wlan_common::WlanMacRole>::FromExternal(out_supported_mac_roles);
  completer.ReplySuccess(reply_data);
}

const fidl::Array<uint8_t, 6> NULL_MAC_ADDR{0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void Device::CreateIface(CreateIfaceRequestView request, CreateIfaceCompleter::Sync& completer) {
  ltrace_fn();

  wlan_mac_role_t role = 0;
  zx_status_t status;

  switch (request->req.role) {
    case fuchsia_wlan_common::WlanMacRole::kClient:
      role = WLAN_MAC_ROLE_CLIENT;
      break;
    case fuchsia_wlan_common::WlanMacRole::kAp:
      role = WLAN_MAC_ROLE_AP;
      break;
    case fuchsia_wlan_common::WlanMacRole::kMesh:
      role = WLAN_MAC_ROLE_MESH;
      break;
    default:
      completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
  }

  if (!role) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  uint16_t iface_id;
  wlanphy_impl_create_iface_req_t create_req{.role = role,
                                             .mlme_channel = request->req.mlme_channel.release()};
  if (memcmp(request->req.init_sta_addr.data(), NULL_MAC_ADDR.data(), NULL_MAC_ADDR.size()) != 0) {
    create_req.has_init_sta_addr = true;
    std::copy(request->req.init_sta_addr.begin(), request->req.init_sta_addr.end(),
              create_req.init_sta_addr);
  } else {
    create_req.has_init_sta_addr = false;
  }

  status = wlanphy_impl_.ops->create_iface(wlanphy_impl_.ctx, &create_req, &iface_id);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(iface_id);
}

void Device::DestroyIface(DestroyIfaceRequestView request, DestroyIfaceCompleter::Sync& completer) {
  ltrace_fn();

  zx_status_t status;
  status = wlanphy_impl_.ops->destroy_iface(wlanphy_impl_.ctx, request->req.id);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  }
  completer.ReplySuccess();
}

void Device::SetCountry(SetCountryRequestView request, SetCountryCompleter::Sync& completer) {
  ltrace_fn();
  ldebug_device("SetCountry to %s\n", wlan::common::Alpha2ToStr(request->req.alpha2).c_str());

  wlanphy_country_t country;
  memcpy(country.alpha2, request->req.alpha2.data(), WLANPHY_ALPHA2_LEN);
  auto status = wlanphy_impl_.ops->set_country(wlanphy_impl_.ctx, &country);

  if (status != ZX_OK) {
    ldebug_device("SetCountry to %s failed with error %s\n",
                  wlan::common::Alpha2ToStr(request->req.alpha2).c_str(),
                  zx_status_get_string(status));
  }

  completer.Reply(status);
}

void Device::GetCountry(GetCountryRequestView request, GetCountryCompleter::Sync& completer) {
  ltrace_fn();

  wlanphy_country_t country;
  if (zx_status_t status = wlanphy_impl_.ops->get_country(wlanphy_impl_.ctx, &country);
      status != ZX_OK) {
    ldebug_device("GetCountry failed with error %s\n", zx_status_get_string(status));
    completer.ReplyError(status);
    return;
  }

  fuchsia_wlan_device::wire::CountryCode response;
  static_assert(std::size(country.alpha2) == decltype(response.alpha2)::size());
  std::copy(std::begin(country.alpha2), std::end(country.alpha2), response.alpha2.begin());
  ldebug_device("GetCountry returning %s\n", wlan::common::Alpha2ToStr(response.alpha2).c_str());
  completer.ReplySuccess(response);
}

void Device::ClearCountry(ClearCountryRequestView request, ClearCountryCompleter::Sync& completer) {
  ltrace_fn();

  auto status = wlanphy_impl_.ops->clear_country(wlanphy_impl_.ctx);
  if (status != ZX_OK) {
    ldebug_device("ClearCountry failed with error %s\n", zx_status_get_string(status));
  }
  completer.Reply(status);
}

void Device::SetPsMode(SetPsModeRequestView request, SetPsModeCompleter::Sync& completer) {
  ltrace_fn();
  ldebug_device("SetPsMode to %d\n", request->req);

  wlanphy_ps_mode_t ps_mode_req{
      .ps_mode = static_cast<power_save_type_t>(request->req),
  };
  zx_status_t status = wlanphy_impl_.ops->set_ps_mode(wlanphy_impl_.ctx, &ps_mode_req);
  if (status != ZX_OK) {
    ldebug_device("SetPsMode to %d failed with error %s\n", request->req,
                  zx_status_get_string(status));
  }
  completer.Reply(status);
}

void Device::GetPsMode(GetPsModeRequestView request, GetPsModeCompleter::Sync& completer) {
  ltrace_fn();

  wlanphy_ps_mode_t ps_mode;
  if (zx_status_t status = wlanphy_impl_.ops->get_ps_mode(wlanphy_impl_.ctx, &ps_mode);
      status != ZX_OK) {
    ldebug_device("GetPsMode failed with error %s\n", zx_status_get_string(status));
    completer.ReplyError(status);
    return;
  }

  fuchsia_wlan_common::PowerSaveType response =
      static_cast<fuchsia_wlan_common::PowerSaveType>(ps_mode.ps_mode);
  ldebug_device("GetPsMode returning %d\n", response);
  completer.ReplySuccess(response);
}
}  // namespace wlanphy
