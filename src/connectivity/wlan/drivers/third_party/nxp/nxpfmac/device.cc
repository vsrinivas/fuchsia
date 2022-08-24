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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/moal_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/moal_shim.h"

namespace wlan::nxpfmac {

constexpr char kFirmwarePath[] = "nxpfmac/sd8987_wlan.bin";

Device::Device(zx_device_t *parent) : DeviceType(parent) {}

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

    zx_status_t status = Init(&mlan_device_);
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

    populate_callbacks(&mlan_device_);

    mlan_status ml_status = mlan_register(&mlan_device_, &mlan_adapter_);
    if (ml_status != MLAN_STATUS_SUCCESS) {
      NXPF_ERR("mlan_register failed: %d", ml_status);
      return ZX_ERR_INTERNAL;
    }
    status = OnMlanRegistered(mlan_adapter_);
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

void Device::GetSupportedMacRoles(GetSupportedMacRolesRequestView request, fdf::Arena &arena,
                                  GetSupportedMacRolesCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::CreateIface(CreateIfaceRequestView request, fdf::Arena &arena,
                         CreateIfaceCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::DestroyIface(DestroyIfaceRequestView request, fdf::Arena &arena,
                          DestroyIfaceCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::SetCountry(SetCountryRequestView request, fdf::Arena &arena,
                        SetCountryCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::GetCountry(GetCountryRequestView request, fdf::Arena &arena,
                        GetCountryCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::ClearCountry(ClearCountryRequestView request, fdf::Arena &arena,
                          ClearCountryCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::SetPsMode(SetPsModeRequestView request, fdf::Arena &arena,
                       SetPsModeCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Device::GetPsMode(GetPsModeRequestView request, fdf::Arena &arena,
                       GetPsModeCompleter::Sync &completer) {
  NXPF_ERR("Not supported");
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
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
  if (mlan_adapter_) {
    // The MLAN shutdown functions still rely on the IRQ thread to be running when shutting down.
    // Shut down MLAN first, then everything else.
    mlan_status ml_status = mlan_shutdown_fw(mlan_adapter_);
    if (ml_status != MLAN_STATUS_SUCCESS) {
      NXPF_ERR("Failed to shutdown firmware: %d", ml_status);
    }
    ml_status = mlan_unregister(mlan_adapter_);
    if (ml_status != MLAN_STATUS_SUCCESS) {
      NXPF_ERR("Failed to unregister mlan: %d", ml_status);
      // Don't stop on this error, we need to shut down everything else anyway.
    }
    mlan_adapter_ = nullptr;
  }

  fidl_dispatcher_.ShutdownAsync();
  zx_status_t status = sync_completion_wait(&fidl_dispatcher_completion_, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to wait for fdf dispatcher to shutdown: %s", zx_status_get_string(status));
    // Keep going and shut everything else down.
  }

  // Shut down the bus specific device.
  Shutdown();
}

zx_status_t Device::InitFirmware() {
  std::vector<uint8_t> firmware_data;
  zx_status_t status = LoadFirmwareData(kFirmwarePath, &firmware_data);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to load firmware data from '%s': %s", kFirmwarePath,
             zx_status_get_string(status));
    return status;
  }

  mlan_fw_image fw = {
      .pfw_buf = firmware_data.data(),
      .fw_len = static_cast<uint32_t>(firmware_data.size()),
      .fw_reload = false,
  };
  mlan_status ml_status = mlan_dnld_fw(mlan_adapter_, &fw);
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

  status = OnFirmwareInitialized();
  if (status != ZX_OK) {
    NXPF_ERR("OnFirmwareInitialized failed: %s", zx_status_get_string(status));
    return status;
  }

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
