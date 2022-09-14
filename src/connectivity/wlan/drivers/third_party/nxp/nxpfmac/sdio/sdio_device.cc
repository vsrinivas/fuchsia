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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/sdio/sdio_device.h"

#include <lib/async-loop/default.h>
#include <lib/ddk/metadata.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"
#include "src/devices/lib/nxp/include/wifi/wifi-config.h"

namespace wlan::nxpfmac {

constexpr uint32_t kDefaultDevCapMask = 0xFFFFFFFF;
SdioDevice::SdioDevice(zx_device_t* parent) : Device(parent) {}

zx_status_t SdioDevice::Create(zx_device_t* parent_device) {
  zx_status_t status = ZX_OK;

  auto async_loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  if ((status = async_loop->StartThread("nxpfmac_sdio-worker", nullptr)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<SdioDevice> device(new SdioDevice(parent_device));
  device->async_loop_ = std::move(async_loop);

  if ((status = device->DdkAdd(
           ddk::DeviceAddArgs("nxpfmac_sdio-wlanphy").set_proto_id(ZX_PROTOCOL_WLANPHY_IMPL))) !=
      ZX_OK) {
    NXPF_ERR("DdkAdd failed: %s", zx_status_get_string(status));
    return status;
  }
  device.release();  // This now has its lifecycle managed by the devhost.

  // Further initialization is performed in the SdioDevice::Init() DDK hook, invoked by the devhost.
  return ZX_OK;
}

async_dispatcher_t* SdioDevice::GetDispatcher() { return async_loop_->dispatcher(); }

// Populate some mlan_device params from wifi metadata
zx_status_t SdioDevice::InitCfgFromMetaData(mlan_device* mlan_device) {
  // Retrieve wifi metadata
  NxpSdioWifiConfig wifi_config;
  size_t actual;
  zx_status_t status =
      DdkGetMetadata(DEVICE_METADATA_WIFI_CONFIG, &wifi_config, sizeof(wifi_config), &actual);
  if (status != ZX_OK) {
    NXPF_ERR("Unable to retrieve wifi metadata status: %d", status);
    return status;
  }
  mlan_device->sdio_rx_aggr_enable = wifi_config.sdio_rx_aggr_enable;
  mlan_device->fixed_beacon_buffer = wifi_config.fixed_beacon_buffer;
  mlan_device->auto_ds = wifi_config.auto_ds;
  mlan_device->ps_mode = wifi_config.ps_mode;
  mlan_device->max_tx_buf = wifi_config.max_tx_buf;
  mlan_device->cfg_11d = wifi_config.cfg_11d;
  mlan_device->mpa_tx_cfg = MLAN_INIT_PARA_DISABLED;
  mlan_device->mpa_rx_cfg = MLAN_INIT_PARA_DISABLED;
  mlan_device->feature_control = FEATURE_CTRL_DEFAULT;

  if (mlan_device->card_type == CARD_TYPE_SD8801 || mlan_device->card_type == CARD_TYPE_SD8887 ||
      mlan_device->card_type == CARD_TYPE_SD8977 || mlan_device->card_type == CARD_TYPE_SD8978 ||
      mlan_device->card_type == CARD_TYPE_SD8987) {
    mlan_device->feature_control &= ~FEATURE_CTRL_STREAM_2X2;
  }

  mlan_device->dev_cap_mask = kDefaultDevCapMask;

  uint8_t bss_idx = 0;
  if (wifi_config.client_support) {
    mlan_device->bss_attr[bss_idx].bss_type = MLAN_BSS_TYPE_STA;
    mlan_device->bss_attr[bss_idx].frame_type = MLAN_DATA_FRAME_TYPE_ETH_II;
    mlan_device->bss_attr[bss_idx].active = MTRUE;
    mlan_device->bss_attr[bss_idx].bss_priority = 0;
    mlan_device->bss_attr[bss_idx].bss_num = 0;
    mlan_device->bss_attr[bss_idx].bss_virtual = MFALSE;
    bss_idx++;
  }

  if (wifi_config.softap_support) {
    mlan_device->bss_attr[bss_idx].bss_type = MLAN_BSS_TYPE_UAP;
    mlan_device->bss_attr[bss_idx].frame_type = MLAN_DATA_FRAME_TYPE_ETH_II;
    mlan_device->bss_attr[bss_idx].active = MTRUE;
    mlan_device->bss_attr[bss_idx].bss_priority = 0;
    mlan_device->bss_attr[bss_idx].bss_num = 0;
    mlan_device->bss_attr[bss_idx].bss_virtual = MFALSE;
  }

  mlan_device->inact_tmo = wifi_config.inact_tmo;
  mlan_device->hs_wake_interval = wifi_config.hs_wake_interval;
  mlan_device->indication_gpio = (t_u8)wifi_config.indication_gpio;
  return status;
}

zx_status_t SdioDevice::Init(mlan_device* mlan_dev, BusInterface** out_bus) {
  if (bus_) {
    NXPF_ERR("SDIO device already initialized");
    return ZX_ERR_ALREADY_EXISTS;
  }

  zx_status_t status = SdioBus::Create(parent(), mlan_dev, &bus_);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to create SDIO bus: %s", zx_status_get_string(status));
    return status;
  }

  if (!IS_SD(mlan_dev->card_type)) {
    NXPF_ERR("Card type %u is NOT an SD card!", mlan_dev->card_type);
    return ZX_ERR_INTERNAL;
  }

  status = InitCfgFromMetaData(mlan_dev);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to init cfg from metadata: %s", zx_status_get_string(status));
    return status;
  }

  *out_bus = bus_.get();
  return ZX_OK;
}

zx_status_t SdioDevice::LoadFirmware(const char* path, zx::vmo* out_fw, size_t* out_size) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  zx_status_t status = load_firmware(zxdev(), path, &vmo, out_size);
  if (status != ZX_OK) {
    NXPF_ERR("Failed loading firmware file '%s': %s", path, zx_status_get_string(status));
    return status;
  }

  *out_fw = zx::vmo(vmo);

  return ZX_OK;
}

void SdioDevice::Shutdown() {
  if (async_loop_) {
    // Explicitly destroy the async loop before further shutdown to prevent asynchronous tasks
    // from using resources as they are being deallocated.
    async_loop_.reset();
  }
  // Then destroy the bus. This should perform a clean shutdown.
  bus_.reset();
}

}  // namespace wlan::nxpfmac
