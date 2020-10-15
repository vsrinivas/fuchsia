// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/hardware/bluetooth/c/fidl.h>
#include <lib/zx/vmo.h>
#include <zircon/process.h>
#include <zircon/status.h>

#include <future>

#include <ddk/protocol/usb.h>
#include <fbl/auto_lock.h>
#include <fbl/string_printf.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "logging.h"

namespace btatheros {

using ::bt::BufferView;
using ::bt::PacketView;

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
    .get_protocol = [](void* ctx, uint32_t proto_id, void* protocol) -> zx_status_t {
      return static_cast<Device*>(ctx)->DdkGetProtocol(proto_id, protocol);
    },
    .init = [](void* ctx) { return static_cast<Device*>(ctx)->DdkInit(); },
    .unbind = [](void* ctx) { return static_cast<Device*>(ctx)->DdkUnbind(); },
    .release = [](void* ctx) { return static_cast<Device*>(ctx)->DdkRelease(); },
    .message = [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) -> zx_status_t {
      return static_cast<Device*>(ctx)->DdkMessage(msg, txn);
    },
};

Device::Device(zx_device_t* device, bt_hci_protocol_t* hci, usb_protocol_t* usb)
    : parent_(device), hci_(*hci), usb_(*usb), firmware_loaded_(false) {}

zx_status_t Device::Bind() {
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt_hci_atheros",
      .ctx = this,
      .ops = &dev_proto,
      .proto_id = ZX_PROTOCOL_BT_HCI,
  };

  return device_add(parent_, &args, &zxdev_);
}

static void interrupt_complete(void* ctx, usb_request_t* req) {
  if (ctx != nullptr) {
    sync_completion_t* completion = (sync_completion_t*)ctx;
    sync_completion_signal(completion);
  }
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
  if (!fw_vmo) {
    errorf("failed to map firmware\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  infof("Loading nvm: %s\n", fw_filename.c_str());

  BufferView file(reinterpret_cast<void*>(fw_addr), fw_size);

  size_t count = fw_size;
  size_t size = std::min(count, NVM_HDR);
  size_t sent = 0;

  result = usb_control_out(&usb_, USB_TYPE_VENDOR, DFU_DOWNLOAD, 0, 0, ZX_TIME_INFINITE,
                           (void*)file.view(0, size).data(), size);
  if (result != ZX_OK) {
    return result;
  }

  usb_request_t* req;
  result = usb_request_alloc(&req, size, bulk_out_addr_, parent_req_size_);
  if (result != ZX_OK) {
    zxlogf(ERROR, "LoadNVM: Failed to allocate usb request: %d", result);
    return result;
  }

  count -= size;
  sent += size;
  while (count) {
    size = std::min(count, DFU_PACKET_LEN);
    req->size = size;
    req->header.length = size;
    usb_request_copy_to(req, file.view(sent, size).data(), size, 0);
    sync_completion_reset(&completion_);
    usb_request_complete_t complete = {
        .callback = interrupt_complete,
        .ctx = &completion_,
    };
    usb_request_queue(&usb_, req, &complete);
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
  if (!fw_vmo) {
    errorf("failed to map firmware\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  infof("Loading rampatch: %s\n", fw_filename.c_str());

  size_t count = fw_size;
  size_t size = std::min(count, RAMPATCH_HDR);
  size_t sent = 0;

  BufferView file(reinterpret_cast<void*>(fw_addr), fw_size);

  result = usb_control_out(&usb_, USB_TYPE_VENDOR, DFU_DOWNLOAD, 0, 0, ZX_TIME_INFINITE,
                           (void*)file.view(0, size).data(), size);
  usb_request_t* req;
  result = usb_request_alloc(&req, size, bulk_out_addr_, parent_req_size_);
  if (result != ZX_OK) {
    zxlogf(ERROR, "LoadRAM: Failed to allocate usb request: %d", result);
    return result;
  }

  count -= size;
  sent += size;
  while (count) {
    size = std::min(count, DFU_PACKET_LEN);
    req->size = size;
    req->header.length = size;
    usb_request_copy_to(req, file.view(sent, size).data(), size, 0);
    sync_completion_reset(&completion_);
    usb_request_complete_t complete = {
        .callback = interrupt_complete,
        .ctx = &completion_,
    };
    usb_request_queue(&usb_, req, &complete);
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
  size_t actual_read;
  result = usb_control_in(&usb_, USB_TYPE_VENDOR | USB_DIR_IN, GET_TARGET_VERSION, 0, 0,
                          ZX_TIME_INFINITE, &ver, sizeof(ver), &actual_read);

  if (result != ZX_OK) {
    return FailInit(result, "Couldn't get version");
  }

  uint8_t status;
  result = usb_control_in(&usb_, USB_TYPE_VENDOR | USB_DIR_IN, GET_STATUS, 0, 0, ZX_TIME_INFINITE,
                          &status, sizeof(status), &actual_read);

  usb_desc_iter_t iter;
  result = usb_desc_iter_init(&usb_, &iter);
  if (result < 0) {
    return FailInit(result, "Usb iterator failed");
  }

  usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
  if (!intf || intf->bNumEndpoints != 3) {
    usb_desc_iter_release(&iter);
    return FailInit(ZX_ERR_NOT_SUPPORTED, "Unexpected number of usb endpoints");
  }

  usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
  while (endp) {
    if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_out_addr_ = endp->bEndpointAddress;
      }
    }
    endp = usb_desc_iter_next_endpoint(&iter);
  }
  usb_desc_iter_release(&iter);

  if (!bulk_out_addr_) {
    return FailInit(ZX_ERR_NOT_SUPPORTED, "LoadFirmware could not find bulk out endpoint");
  }

  if (!(status & PATCH_UPDATED)) {
    result = LoadRAM(ver);
    if (result != ZX_OK) {
      return FailInit(result, "Failed to load Qualcomm Atheros RAM patches");
    }
  }

  if (!(status & SYSCFG_UPDATED)) {
    result = LoadNVM(ver);
    if (result != ZX_OK) {
      return FailInit(result, "Failed to load Qualcomm Atheros NVM patches");
    }
  }

  auto note = fbl::StringPrintf("loaded successfully");
  return Appear(note.c_str());
}

zx_status_t Device::FailInit(zx_status_t status, const char* note) {
  device_init_reply(zxdev_, status, nullptr);
  errorf("%s: %s", note, zx_status_get_string(status));
  return status;
}

zx_status_t Device::Appear(const char* note) {
  fbl::AutoLock lock(&mutex_);
  errorf("Making visible\n");
  // This will make the device visible and able to be unbound.
  device_init_reply(zxdev_, ZX_OK, nullptr);
  infof("%s\n", note);
  firmware_loaded_ = true;
  return ZX_OK;
}

zx_handle_t Device::MapFirmware(const char* name, uintptr_t* fw_addr, size_t* fw_size) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  size_t size;
  zx_status_t status = load_firmware(zxdev_, name, &vmo, &size);
  if (status != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size, fw_addr);
  if (status != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  *fw_size = size;
  return vmo;
}

void Device::DdkInit() {
  auto f = std::async(std::launch::async, [=]() { LoadFirmware(); });
}

void Device::DdkUnbind() { device_unbind_reply(zxdev_); }

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

zx_status_t Device::OpenCommandChannel(void* ctx, zx_handle_t channel) {
  auto& self = *static_cast<btatheros::Device*>(ctx);
  return bt_hci_open_command_channel(&self.hci_, channel);
}

zx_status_t Device::OpenAclDataChannel(void* ctx, zx_handle_t channel) {
  auto& self = *static_cast<btatheros::Device*>(ctx);
  return bt_hci_open_acl_data_channel(&self.hci_, channel);
}

zx_status_t Device::OpenSnoopChannel(void* ctx, zx_handle_t channel) {
  auto& self = *static_cast<btatheros::Device*>(ctx);
  return bt_hci_open_snoop_channel(&self.hci_, channel);
}

zx_status_t Device::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_bluetooth_Hci_dispatch(this, txn, msg, &fidl_ops_);
}

}  // namespace btatheros
