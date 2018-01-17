// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fbl/string_printf.h>
#include <zircon/device/bt-hci.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zx/vmo.h>

#include "firmware_loader.h"
#include "logging.h"

namespace btintel {

Device::Device(zx_device_t* device, bt_hci_protocol_t* hci)
    : ddk::Device<Device, ddk::Unbindable, ddk::Ioctlable>(device),
      hci_(hci),
      firmware_loaded_(false) {}

zx_status_t Device::Bind(bool secure) {
  zx_status_t status;
  zx::channel acl_channel, cmd_channel;

  // Get the channels
  status =
      bt_hci_open_command_channel(hci_, cmd_channel.reset_and_get_address());
  if (status != ZX_OK) {
    errorf("couldn't open command channel: %s\n", zx_status_get_string(status));
    return status;
  }

  // Find the version and boot params.
  VendorHci cmd_hci(&cmd_channel);

  ReadVersionReturnParams version = cmd_hci.SendReadVersion();

  zx::vmo fw_vmo;
  uintptr_t fw_addr;
  size_t fw_size;
  fbl::String fw_filename;

  if (secure) {
    // If we're already in firmware, we're done.
    if (version.fw_variant == kFirmwareFirmwareVariant) {
      return AddDevice("already loaded");
    }

    // We only know how to load if we're in bootloader.
    if (version.fw_variant != kBootloaderFirmwareVariant) {
      errorf("Unknown firmware variant: 0x%x\n", version.fw_variant);
      return ZX_ERR_NOT_SUPPORTED;
    }

    ReadBootParamsReturnParams boot_params = cmd_hci.SendReadBootParams();

    fw_filename = fbl::StringPrintf("ibt-%d-%d.sfi", version.hw_variant,
                                    boot_params.dev_revid);
    fw_vmo.reset(MapFirmware(fw_filename.c_str(), &fw_addr, &fw_size));
  } else {
    if (version.fw_patch_num > 0) {
      return AddDevice("already patched");
    }

    fw_filename = fbl::StringPrintf(
        "ibt-hw-%x.%x.%x-fw-%x.%x.%x.%x.%x.bseq", version.hw_platform,
        version.hw_variant, version.hw_revision, version.fw_variant,
        version.fw_revision, version.fw_build_num, version.fw_build_week,
        version.fw_build_year);

    fw_vmo.reset(MapFirmware(fw_filename.c_str(), &fw_addr, &fw_size));
    if (!fw_vmo) {
      // Try the fallback patch file
      fw_filename = fbl::StringPrintf("ibt-hw-%x.%x.bseq", version.hw_platform,
                                      version.hw_variant);
      fw_vmo.reset(MapFirmware(fw_filename.c_str(), &fw_addr, &fw_size));
    }
  }

  if (!fw_vmo) {
    errorf("can't load firmware %s\n", fw_filename.c_str());
    return ZX_ERR_NOT_SUPPORTED;
  }

  status =
      bt_hci_open_acl_data_channel(hci_, acl_channel.reset_and_get_address());
  if (status != ZX_OK) {
    errorf("couldn't open ACL channel: %s\n", zx_status_get_string(status));
    return status;
  }

  FirmwareLoader loader(&cmd_channel, &acl_channel);

  FirmwareLoader::LoadStatus result;
  if (secure) {
    result = loader.LoadSfi(reinterpret_cast<void*>(fw_addr), fw_size);
  } else {
    cmd_hci.EnterManufacturerMode();
    result = loader.LoadBseq(reinterpret_cast<void*>(fw_addr), fw_size);
    cmd_hci.ExitManufacturerMode(result == FirmwareLoader::LoadStatus::kPatched
                                     ? MfgDisableMode::kPatchesEnabled
                                     : MfgDisableMode::kNoPatches);
  }

  zx_vmar_unmap(zx_vmar_root_self(), fw_addr, fw_size);

  if (result == FirmwareLoader::LoadStatus::kError) {
    errorf("firmware loading failed!\n");
    return ZX_ERR_BAD_STATE;
  }

  cmd_hci.SendReset();

  // TODO(jamuraa): other systems receive a post-boot event here, should we?

  // TODO(jamuraa): ddc file loading (NET-381)

  auto note = fbl::StringPrintf("%s using %s", secure ? "loaded" : "patched",
                                fw_filename.c_str());
  return AddDevice(note.c_str());
}

zx_status_t Device::AddDevice(const char* success_note) {
  zx_status_t status = DdkAdd("btintel");
  if (status != ZX_OK) {
    errorf("add device failed: %s\n", zx_status_get_string(status));
    return status;
  }
  infof("%s\n", success_note);
  firmware_loaded_ = true;
  return ZX_OK;
}

zx_handle_t Device::MapFirmware(const char* name,
                                uintptr_t* fw_addr,
                                size_t* fw_size) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  size_t size;
  zx_status_t status = load_firmware(zxdev(), name, &vmo, &size);
  if (status != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size,
                       ZX_VM_FLAG_PERM_READ, fw_addr);
  if (status != ZX_OK) {
    errorf("firmware map failed: %s\n", zx_status_get_string(status));
    return ZX_HANDLE_INVALID;
  }
  *fw_size = size;
  return vmo;
}

void Device::DdkUnbind() {
  device_remove(zxdev());
}

void Device::DdkRelease() {
  delete this;
}

zx_status_t Device::DdkIoctl(uint32_t op,
                             const void* in_buf,
                             size_t in_len,
                             void* out_buf,
                             size_t out_len,
                             size_t* actual) {

  ZX_DEBUG_ASSERT(firmware_loaded_);
  if (out_len < sizeof(zx_handle_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  zx_handle_t* reply = (zx_handle_t*)out_buf;

  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  if (op == IOCTL_BT_HCI_GET_COMMAND_CHANNEL) {
    status = bt_hci_open_command_channel(hci_, reply);
  } else if (op == IOCTL_BT_HCI_GET_ACL_DATA_CHANNEL) {
    status = bt_hci_open_acl_data_channel(hci_, reply);
  } else if (op == IOCTL_BT_HCI_GET_SNOOP_CHANNEL) {
    status = bt_hci_open_snoop_channel(hci_, reply);
  }

  if (status != ZX_OK) {
    return status;
  }

  *actual = sizeof(*reply);
  return ZX_OK;
}

}  // namespace btintel
