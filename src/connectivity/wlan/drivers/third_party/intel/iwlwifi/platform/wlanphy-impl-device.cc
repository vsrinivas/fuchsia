// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlanphy-impl-device.h"

#include <zircon/status.h>

#include <memory>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlan-softmac-device.h"

namespace wlan::iwlwifi {

namespace wlanphyimpl_fidl = fuchsia_wlan_wlanphyimpl::wire;

WlanphyImplDevice::WlanphyImplDevice(zx_device_t* parent)
    : ::ddk::Device<WlanphyImplDevice, ::ddk::Initializable, ::ddk::Unbindable,
                    ddk::ServiceConnectable>(parent) {
  auto dispatcher = fdf::Dispatcher::Create(0, "iwlwifi", [&](fdf_dispatcher_t*) {
    if (unbind_txn_)
      unbind_txn_->Reply();
  });
  ZX_ASSERT_MSG(!dispatcher.is_error(), "%s(): Dispatcher created failed: %s\n", __func__,
                zx_status_get_string(dispatcher.status_value()));

  dispatcher_ = std::move(*dispatcher);
}

WlanphyImplDevice::~WlanphyImplDevice() = default;

void WlanphyImplDevice::DdkRelease() { delete this; }

zx_status_t WlanphyImplDevice::DdkServiceConnect(const char* service_name, fdf::Channel channel) {
  // Ensure they are requesting the correct protocol.
  if (std::string_view(service_name) !=
      fidl::DiscoverableProtocolName<fuchsia_wlan_wlanphyimpl::WlanphyImpl>) {
    IWL_ERR(this, "Service name doesn't match. Connection request from a wrong device.\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  fdf::ServerEnd<fuchsia_wlan_wlanphyimpl::WlanphyImpl> server_end(std::move(channel));
  fdf::BindServer<fdf::WireServer<fuchsia_wlan_wlanphyimpl::WlanphyImpl>>(
      dispatcher_.get(), std::move(server_end), this);
  return ZX_OK;
}

void WlanphyImplDevice::GetSupportedMacRoles(fdf::Arena& arena,
                                             GetSupportedMacRolesCompleter::Sync& completer) {
  // The fidl array which will be returned to Wlanphy driver.
  fuchsia_wlan_common::WlanMacRole
      supported_mac_roles_list[fuchsia_wlan_common::wire::kMaxSupportedMacRoles] = {};
  uint8_t supported_mac_roles_count = 0;
  zx_status_t status =
      phy_get_supported_mac_roles(drvdata(), supported_mac_roles_list, &supported_mac_roles_count);
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed get supported mac roles: %s\n", __func__,
            zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  if (supported_mac_roles_count > fuchsia_wlan_common::wire::kMaxSupportedMacRoles) {
    IWL_ERR(this,
            "Too many mac roles returned from iwlwifi driver. Number of supported mac roles got "
            "from driver is %u, but the limitation is: %u\n",
            supported_mac_roles_count, fuchsia_wlan_common::wire::kMaxSupportedMacRoles);
    completer.buffer(arena).ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  auto reply_vector = fidl::VectorView<fuchsia_wlan_common::WlanMacRole>::FromExternal(
      supported_mac_roles_list, supported_mac_roles_count);

  completer.buffer(arena).ReplySuccess(reply_vector);
}

void WlanphyImplDevice::CreateIface(CreateIfaceRequestView request, fdf::Arena& arena,
                                    CreateIfaceCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;

  if (!request->has_role() || !request->has_mlme_channel()) {
    IWL_ERR(this, "%s() missing info in request role(%u), channel(%u)\n", __func__,
            request->has_role(), request->has_mlme_channel());
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  uint16_t out_iface_id;
  wlanphy_impl_create_iface_req_t create_iface_req;

  switch (request->role()) {
    case fuchsia_wlan_common::WlanMacRole::kClient:
      create_iface_req.role = WLAN_MAC_ROLE_CLIENT;
      break;
    case fuchsia_wlan_common::WlanMacRole::kAp:
      create_iface_req.role = WLAN_MAC_ROLE_AP;
      break;
    case fuchsia_wlan_common::WlanMacRole::kMesh:
      create_iface_req.role = WLAN_MAC_ROLE_MESH;
      break;
    default:
      IWL_ERR(this, "Unrecognized role from the request. Requested role: %u\n", request->role());
      completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
      return;
  }

  create_iface_req.mlme_channel = request->mlme_channel().release();

  if ((status = phy_create_iface(drvdata(), &create_iface_req, &out_iface_id)) != ZX_OK) {
    IWL_ERR(this, "%s() failed phy create: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  struct iwl_mvm* mvm = iwl_trans_get_mvm(drvdata());
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[out_iface_id];

  auto wlan_softmac_device =
      std::make_unique<WlanSoftmacDevice>(parent(), drvdata(), out_iface_id, mvmvif);

  if ((status = wlan_softmac_device->DdkAdd(
           ::ddk::DeviceAddArgs("iwlwifi-wlan-softmac").set_proto_id(ZX_PROTOCOL_WLAN_SOFTMAC))) !=
      ZX_OK) {
    IWL_ERR(this, "%s() failed mac device add: %s\n", __func__, zx_status_get_string(status));
    phy_create_iface_undo(drvdata(), out_iface_id);
    completer.buffer(arena).ReplyError(status);
    return;
  }
  wlan_softmac_device.release();

  IWL_INFO("%s() created iface %u\n", __func__, out_iface_id);

  fidl::Arena fidl_arena;
  auto builder = wlanphyimpl_fidl::WlanphyImplCreateIfaceResponse::Builder(fidl_arena);
  builder.iface_id(out_iface_id);
  completer.buffer(arena).ReplySuccess(builder.Build());
}

void WlanphyImplDevice::DestroyIface(DestroyIfaceRequestView request, fdf::Arena& arena,
                                     DestroyIfaceCompleter::Sync& completer) {
  if (!request->has_iface_id()) {
    IWL_ERR(this, "%s() invoked without valid iface id\n", __func__);
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  IWL_INFO("%s() for iface %u\n", __func__, request->iface_id());
  zx_status_t status = phy_destroy_iface(drvdata(), request->iface_id());
  if (status != ZX_OK) {
    completer.buffer(arena).ReplyError(status);
    IWL_ERR(this, "%s() failed destroy iface: %s\n", __func__, zx_status_get_string(status));
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void WlanphyImplDevice::SetCountry(SetCountryRequestView request, fdf::Arena& arena,
                                   SetCountryCompleter::Sync& completer) {
  wlanphy_country_t country;

  if (!request->country.is_alpha2()) {
    IWL_ERR(this, "%s() only alpha2 format is supported\n", __func__);
    completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  memcpy(&country.alpha2[0], &request->country.alpha2()[0], WLANPHY_ALPHA2_LEN);
  zx_status_t status = phy_set_country(drvdata(), &country);
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed set country: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void WlanphyImplDevice::ClearCountry(fdf::Arena& arena, ClearCountryCompleter::Sync& completer) {
  IWL_ERR(this, "%s() not implemented ...\n", __func__);
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void WlanphyImplDevice::GetCountry(fdf::Arena& arena, GetCountryCompleter::Sync& completer) {
  fidl::Array<uint8_t, WLANPHY_ALPHA2_LEN> alpha2;

  // TODO(fxbug.dev/95504): Remove the usage of wlanphy_country_t inside the iwlwifi driver.
  wlanphy_country_t country;
  zx_status_t status = phy_get_country(drvdata(), &country);
  if (status != ZX_OK) {
    completer.buffer(arena).ReplyError(status);
    IWL_ERR(this, "%s() failed get country: %s\n", __func__, zx_status_get_string(status));
    return;
  }

  memcpy(alpha2.begin(), country.alpha2, WLANPHY_ALPHA2_LEN);

  auto out_country = fuchsia_wlan_wlanphyimpl::wire::WlanphyCountry::WithAlpha2(alpha2);
  completer.buffer(arena).ReplySuccess(out_country);
}

void WlanphyImplDevice::SetPsMode(SetPsModeRequestView request, fdf::Arena& arena,
                                  SetPsModeCompleter::Sync& completer) {
  IWL_ERR(this, "%s() not implemented ...\n", __func__);
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void WlanphyImplDevice::GetPsMode(fdf::Arena& arena, GetPsModeCompleter::Sync& completer) {
  IWL_ERR(this, "%s() not implemented ...\n", __func__);
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

}  // namespace wlan::iwlwifi
