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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"

#include <lib/fidl/cpp/wire/arena.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/status.h>

#include <ddktl/fidl.h>
#include <ddktl/init-txn.h>
#include <wlan/common/ieee80211.h>

#include "fidl/fuchsia.wlan.wlanphyimpl/cpp/wire_types.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/feature.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/wlan_interface.h"

namespace wlan {
namespace brcmfmac {
namespace {

constexpr char kNetDevDriverName[] = "brcmfmac-netdev";
constexpr char kClientInterfaceName[] = "brcmfmac-wlan-fullmac-client";
constexpr uint8_t kClientInterfaceId = 0;
constexpr char kApInterfaceName[] = "brcmfmac-wlan-fullmac-ap";
constexpr uint8_t kApInterfaceId = 1;
constexpr uint8_t kMaxBufferParts = 1;

}  // namespace

namespace wlan_llcpp = fuchsia_factory_wlan;

Device::Device(zx_device_t* parent)
    : DeviceType(parent),
      brcmf_pub_(std::make_unique<brcmf_pub>()),
      client_interface_(nullptr),
      ap_interface_(nullptr),
      network_device_(parent, this) {
  brcmf_pub_->device = this;
  for (auto& entry : brcmf_pub_->if2bss) {
    entry = BRCMF_BSSIDX_INVALID;
  }

  // Initialize the recovery trigger for driver, shared by all buses' devices.
  auto recovery_start_callback = std::make_shared<std::function<zx_status_t()>>();
  *recovery_start_callback = std::bind(&brcmf_schedule_recovery_worker, brcmf_pub_.get());
  brcmf_pub_->recovery_trigger =
      std::make_unique<wlan::brcmfmac::RecoveryTrigger>(recovery_start_callback);

  auto dispatcher = fdf::Dispatcher::Create(0, "brcmfmac-wlanphy",
                                            [&](fdf_dispatcher_t*) { completion_.Signal(); });
  ZX_ASSERT_MSG(!dispatcher.is_error(), "%s(): Dispatcher created failed: %s\n", __func__,
                zx_status_get_string(dispatcher.status_value()));
  dispatcher_ = std::move(*dispatcher);
}

Device::~Device() { ShutdownDispatcher(); }

void Device::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = Init();
  if (status == ZX_OK && IsNetworkDeviceBus()) {
    status = network_device_.Init(kNetDevDriverName);
  }
  txn.Reply(status);
}

void Device::DdkRelease() {
  Shutdown();
  delete this;
}

void Device::DdkSuspend(ddk::SuspendTxn txn) {
  Shutdown();
  txn.Reply(ZX_OK, txn.requested_state());
}

zx_status_t Device::DdkServiceConnect(const char* service_name, fdf::Channel channel) {
  if (std::string_view(service_name) !=
      fidl::DiscoverableProtocolName<fuchsia_wlan_wlanphyimpl::WlanphyImpl>) {
    BRCMF_ERR("Service name doesn't match. Connection request from a wrong device.\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  fdf::ServerEnd<fuchsia_wlan_wlanphyimpl::WlanphyImpl> server_end(std::move(channel));
  fdf::BindServer<fdf::WireServer<fuchsia_wlan_wlanphyimpl::WlanphyImpl>>(
      dispatcher_.get(), std::move(server_end), this);
  return ZX_OK;
}

void Device::Get(GetRequestView request, GetCompleter::Sync& _completer) {
  BRCMF_DBG(TRACE, "Enter. cmd %d, len %lu", request->cmd, request->request.count());
  zx_status_t status =
      brcmf_send_cmd_to_firmware(brcmf_pub_.get(), request->iface_idx, request->cmd,
                                 (void*)request->request.data(), request->request.count(), false);
  if (status == ZX_OK) {
    _completer.ReplySuccess(request->request);
  } else {
    _completer.ReplyError(status);
  }
  BRCMF_DBG(TRACE, "Exit");
}

void Device::Set(SetRequestView request, SetCompleter::Sync& _completer) {
  BRCMF_DBG(TRACE, "Enter. cmd %d, len %lu", request->cmd, request->request.count());
  zx_status_t status =
      brcmf_send_cmd_to_firmware(brcmf_pub_.get(), request->iface_idx, request->cmd,
                                 (void*)request->request.data(), request->request.count(), true);
  if (status == ZX_OK) {
    _completer.ReplySuccess();
  } else {
    _completer.ReplyError(status);
  }
  BRCMF_DBG(TRACE, "Exit");
}

brcmf_pub* Device::drvr() { return brcmf_pub_.get(); }

const brcmf_pub* Device::drvr() const { return brcmf_pub_.get(); }

void Device::GetSupportedMacRoles(fdf::Arena& arena,
                                  GetSupportedMacRolesCompleter::Sync& completer) {
  BRCMF_DBG(WLANPHY, "Received request for supported MAC roles from SME dfv2");
  fuchsia_wlan_common::wire::WlanMacRole
      supported_mac_roles_list[fuchsia_wlan_common::wire::kMaxSupportedMacRoles] = {};
  uint8_t supported_mac_roles_count = 0;
  zx_status_t status = WlanInterface::GetSupportedMacRoles(
      brcmf_pub_.get(), supported_mac_roles_list, &supported_mac_roles_count);
  if (status != ZX_OK) {
    BRCMF_ERR("Device::GetSupportedMacRoles() failed to get supported mac roles: %s\n",
              zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  if (supported_mac_roles_count > fuchsia_wlan_common::wire::kMaxSupportedMacRoles) {
    BRCMF_ERR(
        "Device::GetSupportedMacRoles() Too many mac roles returned from brcmfmac driver. Number "
        "of supported max roles got "
        "from driver is %u, but the limitation is: %u\n",
        supported_mac_roles_count, fuchsia_wlan_common::wire::kMaxSupportedMacRoles);
    completer.buffer(arena).ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  auto reply_vector = fidl::VectorView<fuchsia_wlan_common::wire::WlanMacRole>::FromExternal(
      supported_mac_roles_list, supported_mac_roles_count);
  completer.buffer(arena).ReplySuccess(reply_vector);
}

void Device::CreateIface(CreateIfaceRequestView request, fdf::Arena& arena,
                         CreateIfaceCompleter::Sync& completer) {
  std::lock_guard<std::mutex> lock(lock_);
  BRCMF_INFO("Device::CreateIface() creating interface started dfv2");

  if (!request->has_role() || !request->has_mlme_channel()) {
    BRCMF_ERR("Device::CreateIface() missing information in role(%u), channel(%u)",
              request->has_role(), request->has_mlme_channel());
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  zx_status_t status = ZX_OK;
  wireless_dev* wdev = nullptr;
  uint16_t iface_id = 0;
  wlanphy_impl_create_iface_req_t create_iface_req;

  create_iface_req.mlme_channel = request->mlme_channel().release();

  create_iface_req.has_init_sta_addr = request->has_init_sta_addr();
  if (request->has_init_sta_addr())
    memcpy(&create_iface_req.init_sta_addr, &(request->init_sta_addr()),
           fuchsia_wlan_ieee80211::wire::kMacAddrLen);

  switch (request->role()) {
    case fuchsia_wlan_common::wire::WlanMacRole::kClient: {
      create_iface_req.role = WLAN_MAC_ROLE_CLIENT;
      if (client_interface_ != nullptr) {
        BRCMF_ERR("Device::CreateIface() client interface already exists");
        completer.buffer(arena).ReplyError(ZX_ERR_NO_RESOURCES);
        return;
      }

      // If we are operating with manufacturing firmware ensure SoftAP IF is also not present
      if (brcmf_feat_is_enabled(brcmf_pub_.get(), BRCMF_FEAT_MFG)) {
        if (ap_interface_ != nullptr) {
          BRCMF_ERR("Simultaneous mode not supported in mfg FW - Ap IF already exists");
          completer.buffer(arena).ReplyError(ZX_ERR_NO_RESOURCES);
          return;
        }
      }

      if ((status = brcmf_cfg80211_add_iface(brcmf_pub_.get(), kClientInterfaceName, nullptr,
                                             &create_iface_req, &wdev)) != ZX_OK) {
        BRCMF_ERR("Device::CreateIface() failed to create Client interface, %s",
                  zx_status_get_string(status));
        completer.buffer(arena).ReplyError(status);
        return;
      }

      WlanInterface* interface = nullptr;
      if ((status = WlanInterface::Create(this, kClientInterfaceName, wdev, create_iface_req.role,
                                          &interface)) != ZX_OK) {
        completer.buffer(arena).ReplyError(status);
        return;
      }

      client_interface_ = interface;  // The lifecycle of `interface` is owned by the devhost.
      iface_id = kClientInterfaceId;

      break;
    }

    case fuchsia_wlan_common::wire::WlanMacRole::kAp: {
      create_iface_req.role = WLAN_MAC_ROLE_AP;
      if (ap_interface_ != nullptr) {
        BRCMF_ERR("Device::CreateIface() AP interface already exists");
        completer.buffer(arena).ReplyError(ZX_ERR_NO_RESOURCES);
        return;
      }

      // If we are operating with manufacturing firmware ensure client IF is also not present
      if (brcmf_feat_is_enabled(brcmf_pub_.get(), BRCMF_FEAT_MFG)) {
        if (client_interface_ != nullptr) {
          BRCMF_ERR("Simultaneous mode not supported in mfg FW - Client IF already exists");
          completer.buffer(arena).ReplyError(ZX_ERR_NO_RESOURCES);
          return;
        }
      }

      if ((status = brcmf_cfg80211_add_iface(brcmf_pub_.get(), kApInterfaceName, nullptr,
                                             &create_iface_req, &wdev)) != ZX_OK) {
        BRCMF_ERR("Device::CreateIface() failed to create AP interface, %s",
                  zx_status_get_string(status));
        completer.buffer(arena).ReplyError(status);
        return;
      }

      WlanInterface* interface = nullptr;
      if ((status = WlanInterface::Create(this, kApInterfaceName, wdev, create_iface_req.role,
                                          &interface)) != ZX_OK) {
        completer.buffer(arena).ReplyError(status);
        return;
      }

      ap_interface_ = interface;  // The lifecycle of `interface` is owned by the devhost.
      iface_id = kApInterfaceId;

      break;
    }

    default: {
      BRCMF_ERR("Device::CreateIface() MAC role %d not supported", create_iface_req.role);
      completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
    }
  }

  // Log the new iface's role, name, and MAC address
  net_device* ndev = wdev->netdev;

  const char* role = request->role() == fuchsia_wlan_common::wire::WlanMacRole::kClient ? "client"
                     : request->role() == fuchsia_wlan_common::wire::WlanMacRole::kAp   ? "ap"
                     : request->role() == fuchsia_wlan_common::wire::WlanMacRole::kMesh
                         ? "mesh"
                         : "unknown type";
  BRCMF_DBG(WLANPHY, "Created %s iface with netdev:%s id:%d", role, ndev->name, iface_id);
#if !defined(NDEBUG)
  const uint8_t* mac_addr = ndev_to_if(ndev)->mac_addr;
  BRCMF_DBG(WLANPHY, "  address: " FMT_MAC, FMT_MAC_ARGS(mac_addr));
#endif /* !defined(NDEBUG) */

  fidl::Arena fidl_arena;
  auto builder =
      fuchsia_wlan_wlanphyimpl::wire::WlanphyImplCreateIfaceResponse::Builder(fidl_arena);
  builder.iface_id(iface_id);
  completer.buffer(arena).ReplySuccess(builder.Build());
  return;
}

void Device::DestroyIface(DestroyIfaceRequestView request, fdf::Arena& arena,
                          DestroyIfaceCompleter::Sync& completer) {
  std::lock_guard<std::mutex> lock(lock_);

  if (!request->has_iface_id()) {
    BRCMF_ERR("Device::DestroyIface() invoked without valid iface_id");
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  uint16_t iface_id = request->iface_id();
  BRCMF_DBG(WLANPHY, "Destroying interface %d", iface_id);
  switch (iface_id) {
    case kClientInterfaceId: {
      DestroyIface(&client_interface_, [completer = completer.ToAsync(), arena = std::move(arena),
                                        iface_id](auto status) mutable {
        if (status != ZX_OK) {
          BRCMF_ERR("Device::DestroyIface() Error destroying Client interface : %s",
                    zx_status_get_string(status));
          completer.buffer(arena).ReplyError(status);
        } else {
          BRCMF_DBG(WLANPHY, "Interface %d destroyed successfully", iface_id);
          completer.buffer(arena).ReplySuccess();
        }
      });
      return;
    }
    case kApInterfaceId: {
      DestroyIface(&ap_interface_, [completer = completer.ToAsync(), arena = std::move(arena),
                                    iface_id](auto status) mutable {
        if (status != ZX_OK) {
          BRCMF_ERR("Device::DestroyIface() Error destroying AP interface : %s",
                    zx_status_get_string(status));
          completer.buffer(arena).ReplyError(status);
        } else {
          BRCMF_DBG(WLANPHY, "Interface %d destroyed successfully", iface_id);
          completer.buffer(arena).ReplySuccess();
        }
      });
      return;
    }
    default: {
      BRCMF_ERR("Device::DestroyIface() Unknown interface id: %d", iface_id);
      completer.buffer(arena).ReplyError(ZX_ERR_NOT_FOUND);
      return;
    }
  }
}

void Device::SetCountry(SetCountryRequestView request, fdf::Arena& arena,
                        SetCountryCompleter::Sync& completer) {
  BRCMF_DBG(WLANPHY, "Setting country code dfv2");
  wlanphy_country_t country;
  if (!request->country.is_alpha2()) {
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    BRCMF_ERR("Device::SetCountry() Invalid input format of country code.");
    return;
  }
  memcpy(&country.alpha2[0], &request->country.alpha2()[0], WLANPHY_ALPHA2_LEN);
  zx_status_t status = WlanInterface::SetCountry(brcmf_pub_.get(), &country);
  if (status != ZX_OK) {
    BRCMF_ERR("Device::SetCountry() Failed Set country : %s", zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void Device::ClearCountry(fdf::Arena& arena, ClearCountryCompleter::Sync& completer) {
  BRCMF_DBG(WLANPHY, "Clearing country dfv2");
  zx_status_t status = WlanInterface::ClearCountry(brcmf_pub_.get());
  if (status != ZX_OK) {
    BRCMF_ERR("Device::ClearCountry() Failed Clear country : %s", zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void Device::GetCountry(fdf::Arena& arena, GetCountryCompleter::Sync& completer) {
  BRCMF_DBG(WLANPHY, "Received request for country from SME dfv2");
  fidl::Array<uint8_t, WLANPHY_ALPHA2_LEN> alpha2;
  wlanphy_country_t country;
  zx_status_t status = WlanInterface::GetCountry(brcmf_pub_.get(), &country);
  if (status != ZX_OK) {
    BRCMF_ERR("Device::GetCountry() Failed Get country : %s", zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  memcpy(alpha2.begin(), country.alpha2, WLANPHY_ALPHA2_LEN);
  auto out_country = fuchsia_wlan_wlanphyimpl::wire::WlanphyCountry::WithAlpha2(alpha2);
  completer.buffer(arena).ReplySuccess(out_country);
}

void Device::SetPsMode(SetPsModeRequestView request, fdf::Arena& arena,
                       SetPsModeCompleter::Sync& completer) {
  BRCMF_DBG(WLANPHY, "Setting power save mode dfv2");
  if (!request->has_ps_mode()) {
    BRCMF_ERR("Device::SetPsMode() invoked without ps_mode");
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  wlanphy_ps_mode_t ps_mode;

  switch (request->ps_mode()) {
    case fuchsia_wlan_common::wire::PowerSaveType::kPsModeUltraLowPower:
      ps_mode.ps_mode = POWER_SAVE_TYPE_PS_MODE_ULTRA_LOW_POWER;
      break;
    case fuchsia_wlan_common::wire::PowerSaveType::kPsModeLowPower:
      ps_mode.ps_mode = POWER_SAVE_TYPE_PS_MODE_LOW_POWER;
      break;
    case fuchsia_wlan_common::wire::PowerSaveType::kPsModeBalanced:
      ps_mode.ps_mode = POWER_SAVE_TYPE_PS_MODE_BALANCED;
      break;
    case fuchsia_wlan_common::wire::PowerSaveType::kPsModePerformance:
      ps_mode.ps_mode = POWER_SAVE_TYPE_PS_MODE_PERFORMANCE;
      break;
    default:
      BRCMF_ERR("Device::SetPsMode() Invalid Power Save mode in request");
      completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
      return;
  }

  zx_status_t status = brcmf_set_ps_mode(brcmf_pub_.get(), &ps_mode);
  if (status != ZX_OK) {
    BRCMF_ERR("Device::SetPsMode() failed setting ps mode : %s", zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void Device::GetPsMode(fdf::Arena& arena, GetPsModeCompleter::Sync& completer) {
  BRCMF_DBG(WLANPHY, "Received request for PS mode from SME dfv2");
  wlanphy_ps_mode_t out_ps_mode;
  zx_status_t status = brcmf_get_ps_mode(brcmf_pub_.get(), &out_ps_mode);
  if (status != ZX_OK) {
    BRCMF_ERR("Device::GetPsMode() Get Power Save Mode failed");
    completer.buffer(arena).ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }
  fuchsia_wlan_common::wire::PowerSaveType ps_mode;
  switch (out_ps_mode.ps_mode) {
    case POWER_SAVE_TYPE_PS_MODE_ULTRA_LOW_POWER:
      ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModeUltraLowPower;
      break;
    case POWER_SAVE_TYPE_PS_MODE_LOW_POWER:
      ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModeLowPower;
      break;
    case POWER_SAVE_TYPE_PS_MODE_BALANCED:
      ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModeBalanced;
      break;
    case POWER_SAVE_TYPE_PS_MODE_PERFORMANCE:
      ps_mode = fuchsia_wlan_common::wire::PowerSaveType::kPsModePerformance;
      break;
    default:
      BRCMF_ERR("Device::GetPsMode() Incorrect Power Save Mode received");
      completer.buffer(arena).ReplyError(ZX_ERR_NOT_FOUND);
      return;
  }
  fidl::Arena fidl_arena;
  auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplGetPsModeResponse::Builder(fidl_arena);
  builder.ps_mode(ps_mode);
  completer.buffer(arena).ReplySuccess(builder.Build());
}

zx_status_t Device::NetDevInit() { return ZX_OK; }

void Device::NetDevRelease() {
  // Don't need to do anything here, the release of wlanif should take care of all releasing
}

void Device::NetDevStart(wlan::drivers::components::NetworkDevice::Callbacks::StartTxn txn) {
  txn.Reply(ZX_OK);
}

void Device::NetDevStop(wlan::drivers::components::NetworkDevice::Callbacks::StopTxn txn) {
  // Flush all buffers in response to this call. They are no longer valid for use.
  brcmf_flush_buffers(drvr());
  txn.Reply();
}

void Device::NetDevGetInfo(device_info_t* out_info) {
  std::lock_guard<std::mutex> lock(lock_);

  memset(out_info, 0, sizeof(*out_info));
  zx_status_t err = brcmf_get_tx_depth(drvr(), &out_info->tx_depth);
  ZX_ASSERT(err == ZX_OK);
  err = brcmf_get_rx_depth(drvr(), &out_info->rx_depth);
  ZX_ASSERT(err == ZX_OK);
  out_info->rx_threshold = out_info->rx_depth / 3;
  out_info->max_buffer_parts = kMaxBufferParts;
  out_info->max_buffer_length = ZX_PAGE_SIZE;
  out_info->buffer_alignment = ZX_PAGE_SIZE;
  out_info->min_rx_buffer_length = IEEE80211_MSDU_SIZE_MAX;

  out_info->tx_head_length = drvr()->hdrlen;
  brcmf_get_tail_length(drvr(), &out_info->tx_tail_length);
  // No hardware acceleration supported yet.
  out_info->rx_accel_count = 0;
  out_info->tx_accel_count = 0;
}

void Device::NetDevQueueTx(cpp20::span<wlan::drivers::components::Frame> frames) {
  brcmf_start_xmit(drvr(), frames);
}

void Device::NetDevQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count,
                                uint8_t* vmo_addrs[]) {
  brcmf_queue_rx_space(drvr(), buffers_list, buffers_count, vmo_addrs);
}

zx_status_t Device::NetDevPrepareVmo(uint8_t vmo_id, zx::vmo vmo, uint8_t* mapped_address,
                                     size_t mapped_size) {
  return brcmf_prepare_vmo(drvr(), vmo_id, vmo.get(), mapped_address, mapped_size);
}

void Device::NetDevReleaseVmo(uint8_t vmo_id) { brcmf_release_vmo(drvr(), vmo_id); }

void Device::NetDevSetSnoopEnabled(bool snoop) {}

void Device::ShutdownDispatcher() {
  dispatcher_.ShutdownAsync();
  completion_.Wait();
}

void Device::DestroyAllIfaces(void) {
  DestroyIface(&client_interface_, [](auto status) {
    if (status != ZX_OK) {
      BRCMF_ERR("Device::DestroyAllIfaces() : Failed destroying client interface : %s",
                zx_status_get_string(status));
    }
  });
  DestroyIface(&ap_interface_, [](auto status) {
    if (status != ZX_OK) {
      BRCMF_ERR("Device::DestroyAllIfaces() : Failed destroying AP interface : %s",
                zx_status_get_string(status));
    }
  });
}

void Device::DestroyIface(WlanInterface** iface_ptr, fit::callback<void(zx_status_t)> respond) {
  WlanInterface* iface = *iface_ptr;
  zx_status_t status = ZX_OK;
  if (iface == nullptr) {
    BRCMF_ERR("Invalid interface");
    respond(ZX_ERR_NOT_FOUND);
    return;
  }

  wireless_dev* wdev = iface->take_wdev();
  iface->RemovePort();
  if ((status = brcmf_cfg80211_del_iface(brcmf_pub_->config, wdev)) != ZX_OK) {
    BRCMF_ERR("Failed to del iface, status: %s", zx_status_get_string(status));
    iface->set_wdev(wdev);
    respond(status);
    return;
  }

  iface->DdkAsyncRemove([status, respond = std::move(respond)]() mutable { respond(status); });
  *iface_ptr = nullptr;
}

}  // namespace brcmfmac
}  // namespace wlan
