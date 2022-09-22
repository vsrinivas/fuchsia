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

#include <lib/zx/timer.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <ddktl/fidl.h>
#include <ddktl/init-txn.h>
#include <wlan/common/ieee80211.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/align.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/moal_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/moal_shim.h"

namespace wlan::nxpfmac {

constexpr char kClientInterfaceName[] = "nxpfmac-wlan-fullmac-client";
constexpr uint8_t kClientInterfaceId = 0;
constexpr char kApInterfaceName[] = "nxpfmac-wlan-fullmac-ap";
constexpr uint8_t kApInterfaceId = 1;

constexpr char kFirmwarePath[] = "nxpfmac/sdsd8987_combo.bin";
constexpr char kTxPwrWWPath[] = "nxpfmac/txpower_WW.bin";
// constexpr char kTxPwrUSPath[] = "nxpfmac/txpower_US.bin";
constexpr char kWlanCalDataPath[] = "nxpfmac/WlanCalData_sd8987.conf";

Device::Device(zx_device_t *parent) : DeviceType(parent) {
  defer_rx_work_event_ =
      event_handler_.RegisterForEvent(MLAN_EVENT_ID_DRV_DEFER_RX_WORK, [this](pmlan_event event) {
        if (data_plane_) {
          data_plane_->DeferRxWork();
        }
      });
}

void Device::DdkInit(ddk::InitTxn txn) {
  const zx_status_t status = [this]() -> zx_status_t {
    auto dispatcher = fdf::Dispatcher::Create(0, "nxpfmac-sdio-wlanphy", [&](fdf_dispatcher_t *) {
      sync_completion_signal(&fidl_dispatcher_completion_);
    });
    if (dispatcher.is_error()) {
      NXPF_ERR("Failed to create fdf dispatcher: %s", dispatcher.status_string());
      return dispatcher.status_value();
    }
    fidl_dispatcher_ = std::move(*dispatcher);

    zx_status_t status = Init(&mlan_device_, &bus_);
    if (status != ZX_OK) {
      NXPF_ERR("nxpfmac: Init failed: %s", zx_status_get_string(status));
      return status;
    }
    if (mlan_device_.callbacks.moal_read_reg == nullptr ||
        mlan_device_.callbacks.moal_write_reg == nullptr ||
        mlan_device_.callbacks.moal_read_data_sync == nullptr ||
        mlan_device_.callbacks.moal_write_data_sync == nullptr) {
      NXPF_ERR("Bus initialization did not populate bus specific callbacks");
      return ZX_ERR_INTERNAL;
    }
    if (mlan_device_.pmoal_handle == nullptr) {
      NXPF_ERR("Bus initialization did not populate moal handle");
      return ZX_ERR_INTERNAL;
    }
    static_cast<MoalContext *>(mlan_device_.pmoal_handle)->device_ = this;
    static_cast<MoalContext *>(mlan_device_.pmoal_handle)->event_handler_ = &event_handler_;

    populate_callbacks(&mlan_device_);

    mlan_status ml_status = mlan_register(&mlan_device_, &mlan_adapter_);
    if (ml_status != MLAN_STATUS_SUCCESS) {
      NXPF_ERR("mlan_register failed: %d", ml_status);
      return ZX_ERR_INTERNAL;
    }

    auto ioctl_adapter = IoctlAdapter::Create(mlan_adapter_, bus_);
    if (ioctl_adapter.is_error()) {
      NXPF_ERR("Failed to create ioctl adapter: %s", ioctl_adapter.status_string());
      return ioctl_adapter.status_value();
    }
    ioctl_adapter_ = std::move(ioctl_adapter.value());
    static_cast<MoalContext *>(mlan_device_.pmoal_handle)->ioctl_adapter_ = ioctl_adapter_.get();

    status = bus_->OnMlanRegistered(mlan_adapter_);
    if (status != ZX_OK) {
      NXPF_ERR("OnMlanRegistered failed: %s", zx_status_get_string(status));
      return status;
    }

    status = InitFirmware();
    if (status != ZX_OK) {
      NXPF_ERR("Failed to initialize firmware: %s", zx_status_get_string(status));
      return status;
    }

    return ZX_OK;
  }();

  txn.Reply(status);
}

void Device::DdkRelease() {
  PerformShutdown();
  delete this;
}

void Device::DdkSuspend(ddk::SuspendTxn txn) {
  NXPF_INFO("Shutdown requested");

  PerformShutdown();

  NXPF_INFO("Shutdown completed");
  txn.Reply(ZX_OK, txn.requested_state());
}

zx_status_t Device::DdkServiceConnect(const char *service_name, fdf::Channel channel) {
  if (std::string_view(service_name) !=
      fidl::DiscoverableProtocolName<fuchsia_wlan_wlanphyimpl::WlanphyImpl>) {
    NXPF_ERR("Service name doesn't match, expected '%s' but got '%s'",
             fidl::DiscoverableProtocolName<fuchsia_wlan_wlanphyimpl::WlanphyImpl>, service_name);
    return ZX_ERR_NOT_SUPPORTED;
  }
  fdf::ServerEnd<fuchsia_wlan_wlanphyimpl::WlanphyImpl> server_end(std::move(channel));
  fdf::BindServer<fdf::WireServer<fuchsia_wlan_wlanphyimpl::WlanphyImpl>>(
      fidl_dispatcher_.get(), std::move(server_end), this);
  return ZX_OK;
}

void Device::GetSupportedMacRoles(fdf::Arena &arena,
                                  GetSupportedMacRolesCompleter::Sync &completer) {
  fuchsia_wlan_common::wire::WlanMacRole
      supported_mac_roles_list[fuchsia_wlan_common::wire::kMaxSupportedMacRoles] = {};
  uint8_t supported_mac_roles_count = 0;
  IoctlRequest<mlan_ds_bss> bss_req(MLAN_IOCTL_BSS, MLAN_ACT_GET, 0,
                                    {.sub_command = MLAN_OID_BSS_ROLE});
  auto &bss_role = bss_req.UserReq().param.bss_role;

  for (uint8_t i = 0; i < MLAN_MAX_BSS_NUM; i++) {
    // Retrieve the role of BSS at index i
    bss_req.IoctlReq().bss_index = i;

    const IoctlStatus io_status = ioctl_adapter_->IssueIoctlSync(&bss_req);
    if (io_status != IoctlStatus::Success) {
      NXPF_ERR("BSS role get req for bss: %d failed: %d", i, io_status);
      continue;
    }
    switch (bss_role) {
      case MLAN_BSS_TYPE_STA:
        supported_mac_roles_list[supported_mac_roles_count++] =
            fuchsia_wlan_common::wire::WlanMacRole::kClient;
        break;
      case MLAN_BSS_TYPE_UAP:
        supported_mac_roles_list[supported_mac_roles_count++] =
            fuchsia_wlan_common::wire::WlanMacRole::kAp;
        break;
      default:
        NXPF_ERR("Unsupported BSS role: %d at idx: %d", bss_role, i);
    }
  }

  auto reply_vector = fidl::VectorView<fuchsia_wlan_common::wire::WlanMacRole>::FromExternal(
      supported_mac_roles_list, supported_mac_roles_count);
  completer.buffer(arena).ReplySuccess(reply_vector);
}

void Device::CreateIface(CreateIfaceRequestView request, fdf::Arena &arena,
                         CreateIfaceCompleter::Sync &completer) {
  std::lock_guard<std::mutex> lock(lock_);

  if (!request->has_role() || !request->has_mlme_channel()) {
    NXPF_ERR("Missing role(%s) and/or channel(%s)", request->has_role() ? "true" : "false",
             request->has_mlme_channel() ? "true" : "false");
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  uint16_t iface_id = 0;

  switch (request->role()) {
    case fuchsia_wlan_common::wire::WlanMacRole::kClient: {
      if (client_interface_ != nullptr) {
        NXPF_ERR("Client interface already exists");
        completer.buffer(arena).ReplyError(ZX_ERR_ALREADY_EXISTS);
        return;
      }

      WlanInterface *interface = nullptr;
      zx_status_t status = WlanInterface::Create(
          parent(), kClientInterfaceName, kClientInterfaceId, WLAN_MAC_ROLE_CLIENT, &event_handler_,
          ioctl_adapter_.get(), data_plane_.get(), std::move(request->mlme_channel()), &interface);
      if (status != ZX_OK) {
        NXPF_ERR("Could not create client interface: %s", zx_status_get_string(status));
        completer.buffer(arena).ReplyError(status);
        return;
      }

      client_interface_ = interface;  // The lifecycle of `interface` is owned by the devhost.
      iface_id = kClientInterfaceId;

      break;
    }
    case fuchsia_wlan_common::wire::WlanMacRole::kAp: {
      if (ap_interface_ != nullptr) {
        NXPF_ERR("AP interface already exists");
        completer.buffer(arena).ReplyError(ZX_ERR_ALREADY_EXISTS);
        return;
      }

      WlanInterface *interface = nullptr;
      zx_status_t status = WlanInterface::Create(
          parent(), kApInterfaceName, kApInterfaceId, WLAN_MAC_ROLE_AP, &event_handler_,
          ioctl_adapter_.get(), data_plane_.get(), std::move(request->mlme_channel()), &interface);
      if (status != ZX_OK) {
        NXPF_ERR("Could not create AP interface: %s", zx_status_get_string(status));
        completer.buffer(arena).ReplyError(status);
        return;
      }
      ap_interface_ = interface;  // The lifecycle of `interface` is owned by the devhost.
      iface_id = kApInterfaceId;

      break;
    };
    default: {
      NXPF_ERR("MAC role %u not supported", request->role());
      completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
    }
  }

  fidl::Arena fidl_arena;
  auto builder =
      fuchsia_wlan_wlanphyimpl::wire::WlanphyImplCreateIfaceResponse::Builder(fidl_arena);
  builder.iface_id(iface_id);
  completer.buffer(arena).ReplySuccess(builder.Build());
}

void Device::DestroyIface(DestroyIfaceRequestView request, fdf::Arena &arena,
                          DestroyIfaceCompleter::Sync &completer) {
  std::lock_guard<std::mutex> lock(lock_);

  NXPF_INFO("Destroying interface %u", request->iface_id());
  switch (request->iface_id()) {
    case kClientInterfaceId: {
      if (client_interface_ == nullptr) {
        NXPF_ERR("Client interface %u unavailable, skipping destroy.", request->iface_id());
        completer.buffer(arena).ReplyError(ZX_ERR_NOT_FOUND);
        return;
      }
      // Remove the network device port so no additional frames are queued up for transmission. Do
      // this before the TX queue is flushed as part of deleting the interface. That way no stray
      // TX frames can sneak into the TX queue between flushing the queue and removing the port.
      client_interface_->DdkAsyncRemove();
      client_interface_ = nullptr;
      break;
    }
    case kApInterfaceId: {
      if (ap_interface_ == nullptr) {
        NXPF_ERR("AP interface %u unavailable, skipping destroy.", request->iface_id());
        completer.buffer(arena).ReplyError(ZX_ERR_NOT_FOUND);
        return;
      }
      ap_interface_->DdkAsyncRemove();
      ap_interface_ = nullptr;
      break;
    }
    default: {
      NXPF_ERR("AP interface %u unavailable, skipping destroy.", request->iface_id());
      completer.buffer(arena).ReplyError(ZX_ERR_NOT_FOUND);
      return;
    }
  }
  completer.buffer(arena).ReplySuccess();
}

void Device::SetCountry(SetCountryRequestView request, fdf::Arena &arena,
                        SetCountryCompleter::Sync &completer) {
  const uint8_t(&country)[2] = request->country.alpha2().data_;

  // Bss index shouldn't matter here, set it to zero.
  IoctlRequest<mlan_ds_misc_cfg> ioctl_request(
      MLAN_IOCTL_MISC_CFG, MLAN_ACT_SET, 0,
      mlan_ds_misc_cfg{.sub_command = MLAN_OID_MISC_COUNTRY_CODE,
                       .param{.country_code{.country_code{country[0], country[1], '\0'}}}});

  IoctlStatus io_status = ioctl_adapter_->IssueIoctlSync(&ioctl_request);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("Failed to set country '%c%c': %d", country[0], country[1], io_status);
    completer.buffer(arena).ReplyError(ZX_ERR_INTERNAL);
    return;
  }
  completer.buffer(arena).ReplySuccess();
}

void Device::GetCountry(fdf::Arena &arena, GetCountryCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::ClearCountry(fdf::Arena &arena, ClearCountryCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::SetPsMode(SetPsModeRequestView request, fdf::Arena &arena,
                       SetPsModeCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::GetPsMode(fdf::Arena &arena, GetPsModeCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::OnEapolTransmitted(wlan::drivers::components::Frame &&frame, zx_status_t status) {
  // Not yet implemented
}

void Device::OnEapolReceived(wlan::drivers::components::Frame &&frame) {
  // Not yet implemented
}

void Device::OnFirmwareInitComplete(zx_status_t status) {
  if (status == ZX_OK) {
    NXPF_INFO("Firmware initialization complete");
  } else {
    NXPF_ERR("Firmware initialization failed: %s", zx_status_get_string(status));
  }
}

void Device::OnFirmwareShutdownComplete(zx_status_t status) {
  if (status == ZX_OK) {
    NXPF_INFO("Firmware shutdown complete");
  } else {
    NXPF_ERR("Firmware shutdown failed: %s", zx_status_get_string(status));
  }
}

void Device::PerformShutdown() {
  // Shut down the fidl dispatcher first. We don't want any calls coming in while this is happening.
  fidl_dispatcher_.ShutdownAsync();
  zx_status_t status = sync_completion_wait(&fidl_dispatcher_completion_, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to wait for fdf dispatcher to shutdown: %s", zx_status_get_string(status));
    // Keep going and shut everything else down.
  }

  if (mlan_adapter_) {
    // The MLAN shutdown functions still rely on the IRQ thread to be running when shutting down.
    // Shut down MLAN first, then everything else.
    mlan_status ml_status = mlan_shutdown_fw(mlan_adapter_);
    if (ml_status != MLAN_STATUS_SUCCESS) {
      NXPF_ERR("Failed to shutdown firmware: %d", ml_status);
    }

    // Shut down the bus specific device. This should stop any IRQ thread from running, otherwise it
    // might access data that will go away during mlan_unregister.
    Shutdown();

    ml_status = mlan_unregister(mlan_adapter_);
    if (ml_status != MLAN_STATUS_SUCCESS) {
      NXPF_ERR("Failed to unregister mlan: %d", ml_status);
      // Don't stop on this error, we need to shut down everything else anyway.
    }
    mlan_adapter_ = nullptr;
  }
}

zx_status_t Device::InitFirmware() {
  std::vector<uint8_t> firmware_data;
  zx_status_t status = LoadFirmwareData(kFirmwarePath, &firmware_data);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to load firmware data from '%s': %s", kFirmwarePath,
             zx_status_get_string(status));
    return status;
  }

  std::vector<uint8_t> tx_power_data;
  status = LoadFirmwareData(kTxPwrWWPath, &tx_power_data);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to load calibration data from '%s': %s", kTxPwrWWPath,
             zx_status_get_string(status));
    return status;
  }

  std::vector<uint8_t> calibration_data;
  status = LoadFirmwareData(kWlanCalDataPath, &calibration_data);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to load calibration data from '%s': %s", kWlanCalDataPath,
             zx_status_get_string(status));
    return status;
  }

  mlan_init_param init_param = {
      .ptxpwr_data_buf = tx_power_data.data(),
      .txpwr_data_len = static_cast<uint32_t>(tx_power_data.size()),
      .pcal_data_buf = calibration_data.data(),
      .cal_data_len = static_cast<uint32_t>(calibration_data.size()),
  };
  mlan_status ml_status = mlan_set_init_param(mlan_adapter_, &init_param);
  if (ml_status != MLAN_STATUS_SUCCESS) {
    NXPF_ERR("mlan_set_init_param failed: %d", ml_status);
    return ZX_ERR_INTERNAL;
  }

  mlan_fw_image fw = {
      .pfw_buf = firmware_data.data(),
      .fw_len = static_cast<uint32_t>(firmware_data.size()),
      .fw_reload = false,
  };
  ml_status = mlan_dnld_fw(mlan_adapter_, &fw);
  if (ml_status != MLAN_STATUS_SUCCESS) {
    NXPF_ERR("mlan_dnld_fw failed: %d", ml_status);
    return ZX_ERR_INTERNAL;
  }

  ml_status = mlan_init_fw(mlan_adapter_);
  // Firmware initialization could be asynchronous and in that case will return pending. When
  // initialization is complete moal_init_fw_complete will be called.
  if (ml_status != MLAN_STATUS_SUCCESS && ml_status != MLAN_STATUS_PENDING) {
    NXPF_ERR("mlan_init_fw failed: %d", ml_status);
    return ZX_ERR_INTERNAL;
  }

  status = bus_->OnFirmwareInitialized();
  if (status != ZX_OK) {
    NXPF_ERR("OnFirmwareInitialized failed: %s", zx_status_get_string(status));
    return status;
  }

  status = DataPlane::Create(parent(), this, bus_, mlan_adapter_, &data_plane_);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to create data plane: %s", zx_status_get_string(status));
    return status;
  }
  static_cast<MoalContext *>(mlan_device_.pmoal_handle)->data_plane_ = data_plane_.get();

  return ZX_OK;
}

zx_status_t Device::LoadFirmwareData(const char *path, std::vector<uint8_t> *data_out) {
  zx::vmo vmo;
  size_t vmo_size = 0;
  zx_status_t status = LoadFirmware(path, &vmo, &vmo_size);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to load firmware data '%s': %s", path, zx_status_get_string(status));
    return status;
  }
  if (vmo_size > std::numeric_limits<uint32_t>::max()) {
    NXPF_ERR("Firmware data in '%s' exceeds maximum size of 4 GiB", path);
    return ZX_ERR_FILE_BIG;
  }

  std::vector<uint8_t> data(vmo_size);

  status = vmo.read(data.data(), 0, data.size());
  if (status != ZX_OK) {
    NXPF_ERR("Failed to read firmware data in '%s' from VMO: %s", path,
             zx_status_get_string(status));
    return status;
  }

  *data_out = std::move(data);

  return ZX_OK;
}

}  // namespace wlan::nxpfmac
