// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/protocol/usb.h>
#include <ddk/usb/usb.h>
#include <usb/usb-request.h>
#include <fbl/auto_lock.h>
#include <fbl/string_printf.h>
#include <lib/zx/vmo.h>
#include <zircon/device/bt-hci.h>
#include <zircon/process.h>
#include <zircon/status.h>

#include "logging.h"

namespace btatheros {

using ::btlib::common::BufferView;
using ::btlib::common::PacketView;

// hard coded for Qualcomm Atheros chipset 0CF3:E300
static constexpr size_t GET_TARGET_VERSION = 0x09;
static constexpr size_t GET_STATUS = 0x05;
static constexpr size_t DFU_DOWNLOAD = 0x01;
static constexpr size_t DFU_PACKET_LEN = 4096;
static constexpr size_t PATCH_UPDATED = 0x80;
static constexpr size_t SYSCFG_UPDATED = 0x40;
static constexpr size_t RAMPATCH_HDR = 28;
static constexpr size_t NVM_HDR = 4;

static zx_protocol_device_t dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = [](void* ctx, uint32_t proto_id,
                       void* protocol) -> zx_status_t {
      return static_cast<Device*>(ctx)->DdkGetProtocol(proto_id, protocol);
    },
    .ioctl = [](void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                void* out_buf, size_t out_len,
                size_t* out_actual) -> zx_status_t {
      return static_cast<Device*>(ctx)->DdkIoctl(op, in_buf, in_len, out_buf,
                                                 out_len, out_actual);
    },
};

Device::Device(zx_device_t* device, bt_hci_protocol_t* hci, usb_protocol_t* usb)
    : parent_(device), hci_(*hci), usb_(*usb), firmware_loaded_(false) {}

zx_status_t Device::Bind() {
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "btatheros",
      .ctx = this,
      .ops = &dev_proto,
      .proto_id = ZX_PROTOCOL_BT_HCI,
      .flags = DEVICE_ADD_INVISIBLE,
  };

  return device_add(parent_, &args, &zxdev_);
}

static void interrupt_complete(usb_request_t* req, void* cookie) {
  if (cookie != nullptr) {
    sync_completion_t* completion = (sync_completion_t*)cookie;
    sync_completion_signal(completion);
  }
}

zx_status_t Device::UsbRequest(usb_request_t** reqptr) {
  usb_desc_iter_t iter;

  zx_status_t result = usb_desc_iter_init(&usb_, &iter);
  if (result < 0) {
    errorf("usb iterator failed: %s\n", zx_status_get_string(result));
    return result;
  }

  usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
  if (!intf || intf->bNumEndpoints != 3) {
    usb_desc_iter_release(&iter);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t bulk_out_addr = 0;

  usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
  while (endp) {
    if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_out_addr = endp->bEndpointAddress;
      }
    }
    endp = usb_desc_iter_next_endpoint(&iter);
  }
  usb_desc_iter_release(&iter);

  if (!bulk_out_addr) {
    errorf("bind could not find bulk out endpoint\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  result = usb_request_alloc(reqptr, DFU_PACKET_LEN, bulk_out_addr, parent_req_size_);
  if (result != ZX_OK) {
      return result;
  }
  usb_request_t* req = *reqptr;
  req->complete_cb = interrupt_complete;
  req->cookie = &completion_;
  return result;
}

zx_status_t Device::LoadNVM(const qca_version& version) {
  zx_status_t result = 0;
  zx::vmo fw_vmo;
  uintptr_t fw_addr;
  size_t fw_size;
  fbl::String fw_filename;
  fbl::AutoLock lock(&mutex_);

  fw_filename = fbl::StringPrintf("nvm_usb_%08X.bin", version.rom_version);
  fw_vmo.reset(MapFirmware(fw_filename.c_str(), &fw_addr, &fw_size));
  infof("Loading nvm: %s\n", fw_filename.c_str());

  BufferView file(reinterpret_cast<void*>(fw_addr), fw_size);

  size_t count = fw_size;
  size_t size = std::min(count, NVM_HDR);
  size_t sent = 0;

  result = usb_control(&usb_, USB_TYPE_VENDOR, DFU_DOWNLOAD, 0, 0,
                       (void*)file.view(0, size).data(), size, ZX_TIME_INFINITE,
                       NULL);
  if (result != ZX_OK) {
    return result;
  }

  usb_request_t* req = NULL;
  result = UsbRequest(&req);
  if (result != ZX_OK) {
    return result;
  }

  count -= size;
  sent += size;
  while (count) {
    size = std::min(count, DFU_PACKET_LEN);

    usb_request_copy_to(req, file.view(sent, size).data(), size, 0);
    req->complete_cb = interrupt_complete;
    req->cookie = &completion_;
    sync_completion_reset(&completion_);
    usb_request_queue(&usb_, req);
    sync_completion_wait(&completion_, ZX_TIME_INFINITE);

    if (req->response.status != ZX_OK) {
      result = req->response.status;
      break;
    }

    count -= size;
    sent += size;
  }

  zx_vmar_unmap(zx_vmar_root_self(), fw_addr, fw_size);
  usb_request_release(req);
  return result;
}

zx_status_t Device::LoadRAM(const qca_version& version) {
  zx_status_t result;
  zx::vmo fw_vmo;
  uintptr_t fw_addr;
  size_t fw_size;
  fbl::String fw_filename;
  fbl::AutoLock lock(&mutex_);

  fw_filename = fbl::StringPrintf("rampatch_usb_%08X.bin", version.rom_version);
  fw_vmo.reset(MapFirmware(fw_filename.c_str(), &fw_addr, &fw_size));
  infof("Loading rampatch: %s\n", fw_filename.c_str());

  size_t count = fw_size;
  size_t size = std::min(count, RAMPATCH_HDR);
  size_t sent = 0;

  BufferView file(reinterpret_cast<void*>(fw_addr), fw_size);

  result = usb_control(&usb_, USB_TYPE_VENDOR, DFU_DOWNLOAD, 0, 0,
                       (void*)file.view(0, size).data(), size, ZX_TIME_INFINITE,
                       NULL);

  usb_request_t* req;
  result = UsbRequest(&req);
  if (result != ZX_OK) {
    return result;
  }

  count -= size;
  sent += size;
  while (count) {
    size = std::min(count, DFU_PACKET_LEN);

    usb_request_copy_to(req, file.view(sent, size).data(), size, 0);
    req->complete_cb = interrupt_complete;
    req->cookie = &completion_;
    sync_completion_reset(&completion_);
    usb_request_queue(&usb_, req);
    sync_completion_wait(&completion_, ZX_TIME_INFINITE);

    if (req->response.status != ZX_OK) {
      result = req->response.status;
      break;
    }

    count -= size;
    sent += size;
  }

  zx_vmar_unmap(zx_vmar_root_self(), fw_addr, fw_size);
  usb_request_release(req);
  return result;
}

zx_status_t Device::LoadFirmware() {
  zx_status_t result;
  usb_device_descriptor_t dev_desc;

  parent_req_size_ = usb_get_request_size(&usb_);
  ZX_DEBUG_ASSERT(parent_req_size_ != 0);

  usb_get_device_descriptor(&usb_, &dev_desc);

  struct qca_version ver;
  result = usb_control(&usb_, USB_TYPE_VENDOR | USB_DIR_IN, GET_TARGET_VERSION,
                       0, 0, &ver, sizeof(ver), ZX_TIME_INFINITE, NULL);

  if (result != ZX_OK) {
    errorf("couldn't get version");
    return result;
  }

  uint8_t status;
  result = usb_control(&usb_, USB_TYPE_VENDOR | USB_DIR_IN, GET_STATUS, 0, 0,
                       &status, sizeof(status), ZX_TIME_INFINITE, NULL);

  if (!(status & PATCH_UPDATED)) {
    result = LoadRAM(ver);
    if (result != ZX_OK) {
      return Remove(result, "Failed to load Qualcomm Atheros RAM patches");
    }
  }

  if (!(status & SYSCFG_UPDATED)) {
    result = LoadNVM(ver);
    if (result != ZX_OK) {
      return Remove(result, "Failed to load Qualcomm Atheros NVM patches");
    }
  }

  auto note = fbl::StringPrintf("loaded successfully");
  return Appear(note.c_str());
}

zx_status_t Device::Remove(zx_status_t status, const char* note) {
  device_remove(zxdev_);
  errorf("%s: %s", note, zx_status_get_string(status));
  return status;
}

zx_status_t Device::Appear(const char* note) {
  fbl::AutoLock lock(&mutex_);
  errorf("Making visible\n");
  device_make_visible(zxdev_);
  infof("%s\n", note);
  firmware_loaded_ = true;
  return ZX_OK;
}

zx_handle_t Device::MapFirmware(const char* name, uintptr_t* fw_addr,
                                size_t* fw_size) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  size_t size;
  zx_status_t status = load_firmware(zxdev_, name, &vmo, &size);
  if (status != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size,
                       fw_addr);
  if (status != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  *fw_size = size;
  return vmo;
}

void Device::DdkUnbind() { device_remove(zxdev_); }

void Device::DdkRelease() { delete this; }

zx_status_t Device::DdkGetProtocol(uint32_t proto_id, void* out_proto) {
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  bt_hci_protocol_t* hci_proto = static_cast<bt_hci_protocol_t*>(out_proto);

  // Forward the underlying bt-transport ops.
  hci_proto->ops = hci_.ops;
  hci_proto->ctx = hci_.ctx;

  return ZX_OK;
}

zx_status_t Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* actual) {
  fbl::AutoLock lock(&mutex_);
  ZX_DEBUG_ASSERT(firmware_loaded_);
  if (out_len < sizeof(zx_handle_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  zx_handle_t* reply = (zx_handle_t*)out_buf;

  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  if (op == IOCTL_BT_HCI_GET_COMMAND_CHANNEL) {
    status = bt_hci_open_command_channel(&hci_, reply);
  } else if (op == IOCTL_BT_HCI_GET_ACL_DATA_CHANNEL) {
    status = bt_hci_open_acl_data_channel(&hci_, reply);
  } else if (op == IOCTL_BT_HCI_GET_SNOOP_CHANNEL) {
    status = bt_hci_open_snoop_channel(&hci_, reply);
  }

  if (status != ZX_OK) {
    return status;
  }

  *actual = sizeof(*reply);
  return ZX_OK;
}

}  // namespace btatheros
