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
    : ddk::Device<Device, ddk::Unbindable, ddk::Ioctlable>(device), hci_(hci), firmware_loaded_(false) {}

zx_status_t Device::Bind() {
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

  // If we're already in firmware, we're done.
  if (version.fw_variant == kFirmwareFirmwareVariant) {
    infof("Firmware already loaded, continuing..\n");
    status = DdkAdd("btintel");
    if (status != ZX_OK) {
      errorf("add device failed: %s\n", zx_status_get_string(status));
    }
    firmware_loaded_ = true;
    return status;
  }

  // We only know how to load if we're in bootloader.
  if (version.fw_variant != kBootloaderFirmwareVariant) {
    errorf("Unknown firmware variant: 0x%x\n", version.fw_variant);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ReadBootParamsReturnParams boot_params = cmd_hci.SendReadBootParams();

  auto filename = fbl::StringPrintf("ibt-%d-%d.sfi", version.hw_variant, boot_params.dev_revid);

  status =
      bt_hci_open_acl_data_channel(hci_, acl_channel.reset_and_get_address());
  if (status != ZX_OK) {
    errorf("couldn't open ACL channel: %s\n", zx_status_get_string(status));
    return status;
  }

  FirmwareLoader loader(&cmd_channel, &acl_channel);

  size_t fw_size;
  zx::vmo fw_vmo;
  status = load_firmware(zxdev(), filename.c_str(),
                         fw_vmo.reset_and_get_address(), &fw_size);
  if (status != ZX_OK) {
    errorf("can't find firmware file %s: %s\n", filename.c_str(),
          zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }

  uintptr_t fw_addr;

  status = zx_vmar_map(zx_vmar_root_self(), 0, fw_vmo.get(), 0, fw_size,
                       ZX_VM_FLAG_PERM_READ, &fw_addr);
  if (status != ZX_OK) {
    errorf("firmware file map failed: %s\n", zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!loader.LoadSfi(reinterpret_cast<void*>(fw_addr), fw_size)) {
    errorf("firmware file loading failed!\n");
    return ZX_ERR_BAD_STATE;
  }

  cmd_hci.SendReset();
  // TODO(jamuraa): other systems receive a post-boot event here, should we?

  // TODO(jamuraa): bseq / ddc file loading, after the reset (NET-335)

  status = DdkAdd("btintel");
  if (status != ZX_OK) {
    errorf("add device failed: %s\n", zx_status_get_string(status));
  }
  firmware_loaded_ = true;
  return status;
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
