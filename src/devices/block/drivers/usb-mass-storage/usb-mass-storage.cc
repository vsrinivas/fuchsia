// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-mass-storage.h"

#include <endian.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "block.h"

// comment the next line if you don't want debug messages
#define DEBUG 0
#ifdef DEBUG
#define DEBUG_PRINT(x) printf x
#else
#define DEBUG_PRINT(x) \
  do {                 \
  } while (0)
#endif

namespace {
void ReqComplete(void* ctx, usb_request_t* req) {
  if (ctx) {
    sync_completion_signal(static_cast<sync_completion_t*>(ctx));
  }
}
}  // namespace

namespace ums {

void UsbMassStorageDevice::QueueTransaction(Transaction* txn) {
  {
    fbl::AutoLock l(&txn_lock_);
    list_add_tail(&queued_txns_, &txn->node);
  }
  sync_completion_signal(&txn_completion_);
}

void UsbMassStorageDevice::DdkRelease() {
  if (cbw_req_) {
    usb_request_release(cbw_req_);
  }
  if (data_req_) {
    usb_request_release(data_req_);
  }
  if (csw_req_) {
    usb_request_release(csw_req_);
  }
  if (data_transfer_req_) {
    usb_request_release(data_transfer_req_);
  }
  delete this;
}

void UsbMassStorageDevice::DdkUnbindDeprecated() {
  // terminate our worker thread
  {
    fbl::AutoLock l(&txn_lock_);
    dead_ = true;
  }
  sync_completion_signal(&txn_completion_);

  // wait for worker thread to finish before removing devices
  thrd_join(worker_thread_, NULL);
  for (uint8_t lun = 0; lun <= max_lun_; lun++) {
    auto dev = block_devs_[lun];
    const auto& params = dev->GetBlockDeviceParameters();
    if (params.device_added) {
      dev->DdkRemoveDeprecated();
    }
  }
  // Wait for remaining requests to complete
  while (pending_requests_.load()) {
    sync_completion_wait(&txn_completion_, ZX_SEC(1));
  }
  DdkRemoveDeprecated();
}

void UsbMassStorageDevice::RequestQueue(usb_request_t* request,
                                        const usb_request_complete_t* completion) {
  fbl::AutoLock l(&txn_lock_);
  pending_requests_++;
  UsbRequestContext context;
  context.completion = *completion;
  usb_request_complete_t complete;
  complete.callback = [](void* ctx, usb_request_t* req) {
    UsbRequestContext context;
    memcpy(&context,
           reinterpret_cast<unsigned char*>(req) +
               reinterpret_cast<UsbMassStorageDevice*>(ctx)->parent_req_size_,
           sizeof(context));
    reinterpret_cast<UsbMassStorageDevice*>(ctx)->pending_requests_--;
    context.completion.callback(context.completion.ctx, req);
  };
  complete.ctx = this;
  memcpy(reinterpret_cast<unsigned char*>(request) + parent_req_size_, &context, sizeof(context));
  usb_.RequestQueue(request, &complete);
}

// Performs the object initialization.
zx_status_t UsbMassStorageDevice::Init() {
  dead_ = false;
  zxlogf(INFO, "UMS: parent: '%s'", device_get_name(parent()));
  // Add root device, which will contain block devices for logical units
  zx_status_t status = DdkAdd("ums", DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INVISIBLE);
  if (status != ZX_OK) {
    delete this;
    return status;
  }
  auto call = fbl::MakeAutoCall([&]() { DdkRemoveDeprecated(); });
  usb::UsbDevice usb(parent());
  if (!usb.is_valid()) {
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }

  // find our endpoints
  std::optional<usb::InterfaceList> interfaces;
  status = usb::InterfaceList::Create(usb, true, &interfaces);
  if (status != ZX_OK) {
    return status;
  }
  auto interface = interfaces->begin();
  const usb_interface_descriptor_t* interface_descriptor = interface->descriptor();
  uint8_t interface_number = interface_descriptor->bInterfaceNumber;
  uint8_t bulk_in_addr = 0;
  uint8_t bulk_out_addr = 0;
  size_t bulk_in_max_packet = 0;
  size_t bulk_out_max_packet = 0;

  if (interface == interfaces->end()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (interface_descriptor->bNumEndpoints < 2) {
    DEBUG_PRINT(
        ("UMS:ums_bind wrong number of endpoints: %d\n", interface_descriptor->bNumEndpoints));
    return ZX_ERR_NOT_SUPPORTED;
  }

  for (auto ep_itr : interfaces->begin()->GetEndpointList()) {
    const usb_endpoint_descriptor_t* endp = &ep_itr.descriptor;
    if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_out_addr = endp->bEndpointAddress;
        bulk_out_max_packet = usb_ep_max_packet(endp);
      }
    } else {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_in_addr = endp->bEndpointAddress;
        bulk_in_max_packet = usb_ep_max_packet(endp);
      }
    }
  }

  if (!bulk_in_max_packet || !bulk_out_max_packet) {
    DEBUG_PRINT(("UMS:ums_bind could not find endpoints\n"));
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t max_lun;
  size_t out_length;
  status = usb.ControlIn(USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_REQ_GET_MAX_LUN,
                         0x00, 0x00, ZX_TIME_INFINITE, &max_lun, sizeof(max_lun), &out_length);

  if (status == ZX_ERR_IO_REFUSED) {
    // Devices that do not support multiple LUNS may stall this command.
    // See USB Mass Storage Class Spec. 3.2 Get Max LUN.
    // Clear the stall.
    usb.ResetEndpoint(0);
    zxlogf(INFO, "Device does not support multiple LUNs");
    max_lun = 0;
  } else if (status != ZX_OK) {
    return status;
  } else if (out_length != sizeof(max_lun)) {
    return ZX_ERR_BAD_STATE;
  }
  fbl::AllocChecker checker;
  fbl::RefPtr<UmsBlockDevice>* raw_array;
  raw_array = new (&checker) fbl::RefPtr<UmsBlockDevice>[max_lun + 1];
  if (!checker.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  block_devs_ = fbl::Array(raw_array, max_lun + 1);
  DEBUG_PRINT(("UMS:Max lun is: %u\n", max_lun));
  max_lun_ = max_lun;
  for (uint8_t lun = 0; lun <= max_lun; lun++) {
    auto dev = fbl::MakeRefCountedChecked<UmsBlockDevice>(
        &checker, zxdev(), lun, [this](ums::Transaction* txn) { QueueTransaction(txn); });
    if (!checker.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    block_devs_[lun] = dev;
  }

  list_initialize(&queued_txns_);
  sync_completion_reset(&txn_completion_);

  usb_ = usb;
  bulk_in_addr_ = bulk_in_addr;
  bulk_out_addr_ = bulk_out_addr;
  bulk_in_max_packet_ = bulk_in_max_packet;
  bulk_out_max_packet_ = bulk_out_max_packet;
  interface_number_ = interface_number;

  size_t max_in = usb.GetMaxTransferSize(bulk_in_addr);
  size_t max_out = usb.GetMaxTransferSize(bulk_out_addr);
  max_transfer_ = (max_in < max_out ? max_in : max_out);

  parent_req_size_ = usb.GetRequestSize();
  ZX_DEBUG_ASSERT(parent_req_size_ != 0);
  size_t usb_request_size = parent_req_size_ + sizeof(UsbRequestContext);
  status = usb_request_alloc(&cbw_req_, sizeof(ums_cbw_t), bulk_out_addr, usb_request_size);
  if (status != ZX_OK) {
    return status;
  }
  status = usb_request_alloc(&data_req_, PAGE_SIZE, bulk_in_addr, usb_request_size);
  if (status != ZX_OK) {
    return status;
  }
  status = usb_request_alloc(&csw_req_, sizeof(ums_csw_t), bulk_in_addr, usb_request_size);
  if (status != ZX_OK) {
    return status;
  }

  status = usb_request_alloc(&data_transfer_req_, 0, bulk_in_addr, usb_request_size);
  if (status != ZX_OK) {
    return status;
  }

  tag_send_ = tag_receive_ = 8;

  if (status != ZX_OK) {
    return status;
  }

  int ret = thrd_create(
      &worker_thread_,
      [](void* thisptr) { return static_cast<UsbMassStorageDevice*>(thisptr)->WorkerThread(); },
      this);
  if (ret != thrd_success) {
    return ZX_ERR_NO_MEMORY;
  }
  call.cancel();
  return status;
}

zx_status_t UsbMassStorageDevice::Reset() {
  // UMS Reset Recovery. See section 5.3.4 of
  // "Universal Serial Bus Mass Storage Class Bulk-Only Transport"
  DEBUG_PRINT(("UMS: performing reset recovery\n"));
  // Step 1: Send  Bulk-Only Mass Storage Reset
  zx_status_t status =
      usb_.ControlOut(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_REQ_RESET, 0,
                      interface_number_, ZX_TIME_INFINITE, NULL, 0);
  usb_protocol_t usb;
  usb_.GetProto(&usb);
  if (status != ZX_OK) {
    DEBUG_PRINT(("UMS: USB_REQ_RESET failed %d\n", status));
    return status;
  }
  // Step 2: Clear Feature HALT to the Bulk-In endpoint
  constexpr uint8_t request_type = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT;
  status = usb_.ClearFeature(request_type, USB_ENDPOINT_HALT, bulk_in_addr_, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    DEBUG_PRINT(("UMS: clear endpoint halt failed %d\n", status));
    return status;
  }
  // Step 3: Clear Feature HALT to the Bulk-Out endpoint
  status = usb_.ClearFeature(request_type, USB_ENDPOINT_HALT, bulk_out_addr_, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    DEBUG_PRINT(("UMS: clear endpoint halt failed %d\n", status));
    return status;
  }
  return ZX_OK;
}

void UsbMassStorageDevice::SendCbw(uint8_t lun, uint32_t transfer_length, uint8_t flags,
                                   uint8_t command_len, void* command) {
  usb_request_t* req = cbw_req_;

  ums_cbw_t* cbw;
  zx_status_t status = usb_request_mmap(req, (void**)&cbw);
  if (status != ZX_OK) {
    DEBUG_PRINT(("UMS: usb request mmap failed: %d\n", status));
    return;
  }

  memset(cbw, 0, sizeof(*cbw));
  cbw->dCBWSignature = htole32(CBW_SIGNATURE);
  cbw->dCBWTag = htole32(tag_send_++);
  cbw->dCBWDataTransferLength = htole32(transfer_length);
  cbw->bmCBWFlags = flags;
  cbw->bCBWLUN = lun;
  cbw->bCBWCBLength = command_len;

  // copy command_len bytes from the command passed in into the command_len
  memcpy(cbw->CBWCB, command, command_len);

  sync_completion_t completion;
  usb_request_complete_t complete = {
      .callback = ReqComplete,
      .ctx = &completion,
  };
  RequestQueue(req, &complete);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
}

zx_status_t UsbMassStorageDevice::ReadCsw(uint32_t* out_residue) {
  sync_completion_t completion;
  usb_request_complete_t complete = {
      .callback = ReqComplete,
      .ctx = &completion,
  };

  usb_request_t* csw_request = csw_req_;
  RequestQueue(csw_request, &complete);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  csw_status_t csw_error = VerifyCsw(csw_request, out_residue);

  if (csw_error == CSW_SUCCESS) {
    return ZX_OK;
  } else if (csw_error == CSW_FAILED) {
    return ZX_ERR_BAD_STATE;
  } else {
    // FIXME - best way to handle this?
    // print error and then reset device due to it
    DEBUG_PRINT(
        ("UMS: CSW verify returned error. Check ums-hw.h csw_status_t for enum = %d\n", csw_error));
    Reset();
    return ZX_ERR_INTERNAL;
  }
}

csw_status_t UsbMassStorageDevice::VerifyCsw(usb_request_t* csw_request, uint32_t* out_residue) {
  ums_csw_t csw;
  usb_request_copy_from(csw_request, &csw, sizeof(csw), 0);

  // check signature is "USBS"
  if (letoh32(csw.dCSWSignature) != CSW_SIGNATURE) {
    DEBUG_PRINT(("UMS:invalid csw sig: %08x \n", letoh32(csw.dCSWSignature)));
    return CSW_INVALID;
  }
  // check if tag matches the tag of last CBW
  if (letoh32(csw.dCSWTag) != tag_receive_++) {
    DEBUG_PRINT(("UMS:csw tag mismatch, expected:%08x got in csw:%08x \n", tag_receive_ - 1,
                 letoh32(csw.dCSWTag)));
    return CSW_TAG_MISMATCH;
  }
  // check if success is true or not?
  if (csw.bmCSWStatus == CSW_FAILED) {
    return CSW_FAILED;
  } else if (csw.bmCSWStatus == CSW_PHASE_ERROR) {
    return CSW_PHASE_ERROR;
  }

  if (out_residue) {
    *out_residue = letoh32(csw.dCSWDataResidue);
  }
  return CSW_SUCCESS;
}

void UsbMassStorageDevice::QueueRead(uint16_t transfer_length) {
  // read request sense response
  usb_request_t* read_request = data_req_;
  read_request->header.length = transfer_length;
  usb_request_complete_t complete = {
      .callback = ReqComplete,
      .ctx = NULL,
  };
  RequestQueue(read_request, &complete);
}

zx_status_t UsbMassStorageDevice::Inquiry(uint8_t lun, uint8_t* out_data) {
  // CBW Configuration
  scsi_command6_t command;
  memset(&command, 0, sizeof(command));
  command.opcode = UMS_INQUIRY;
  command.length = UMS_INQUIRY_TRANSFER_LENGTH;
  SendCbw(lun, UMS_INQUIRY_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);
  // read inquiry response
  QueueRead(UMS_INQUIRY_TRANSFER_LENGTH);
  // wait for CSW
  zx_status_t status = ReadCsw(NULL);
  if (status == ZX_OK) {
    usb_request_copy_from(data_req_, out_data, UMS_INQUIRY_TRANSFER_LENGTH, 0);
  }
  return status;
}

zx_status_t UsbMassStorageDevice::TestUnitReady(uint8_t lun) {
  // CBW Configuration
  scsi_command6_t command;
  memset(&command, 0, sizeof(command));
  command.opcode = UMS_TEST_UNIT_READY;
  SendCbw(lun, 0, USB_DIR_IN, sizeof(command), &command);
  // wait for CSW
  return ReadCsw(NULL);
}

zx_status_t UsbMassStorageDevice::RequestSense(uint8_t lun, uint8_t* out_data) {
  // CBW Configuration
  scsi_command6_t command;
  memset(&command, 0, sizeof(command));
  command.opcode = UMS_REQUEST_SENSE;
  command.length = UMS_REQUEST_SENSE_TRANSFER_LENGTH;
  SendCbw(lun, UMS_REQUEST_SENSE_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);

  // read request sense response
  QueueRead(UMS_REQUEST_SENSE_TRANSFER_LENGTH);

  // wait for CSW
  zx_status_t status = ReadCsw(NULL);
  if (status == ZX_OK) {
    usb_request_copy_from(data_req_, out_data, UMS_REQUEST_SENSE_TRANSFER_LENGTH, 0);
  }
  return status;
}

zx_status_t UsbMassStorageDevice::ReadCapacity(uint8_t lun, scsi_read_capacity_10_t* out_data) {
  // CBW Configuration
  scsi_command10_t command;
  memset(&command, 0, sizeof(command));
  command.opcode = UMS_READ_CAPACITY10;
  SendCbw(lun, sizeof(*out_data), USB_DIR_IN, sizeof(command), &command);

  // read capacity10 response
  QueueRead(sizeof(*out_data));

  zx_status_t status = ReadCsw(NULL);
  if (status == ZX_OK) {
    usb_request_copy_from(data_req_, out_data, sizeof(*out_data), 0);
  }
  return status;
}

zx_status_t UsbMassStorageDevice::ReadCapacity(uint8_t lun, scsi_read_capacity_16_t* out_data) {
  // CBW Configuration
  scsi_command16_t command;
  memset(&command, 0, sizeof(command));
  command.opcode = UMS_READ_CAPACITY16;
  // service action = 10, not sure what that means
  command.misc = 0x10;
  command.length = sizeof(*out_data);
  SendCbw(lun, sizeof(*out_data), USB_DIR_IN, sizeof(command), &command);

  // read capacity16 response
  QueueRead(sizeof(*out_data));

  zx_status_t status = ReadCsw(NULL);
  if (status == ZX_OK) {
    usb_request_copy_from(data_req_, out_data, sizeof(*out_data), 0);
  }
  return status;
}
zx_status_t UsbMassStorageDevice::ModeSense(uint8_t lun, uint8_t page, void* data,
                                            uint8_t transfer_length) {
  // CBW Configuration
  scsi_mode_sense_6_command_t command;
  memset(&command, 0, sizeof(command));
  command.opcode = UMS_MODE_SENSE6;
  command.page = page;  // all pages, current values
  command.allocation_length = transfer_length;

  SendCbw(lun, transfer_length, USB_DIR_IN, sizeof(command), &command);

  // read mode sense response
  QueueRead(transfer_length);

  zx_status_t status = ReadCsw(NULL);
  if (status == ZX_OK) {
    usb_request_copy_from(data_req_, data, transfer_length, 0);
  }
  return status;
}

zx_status_t UsbMassStorageDevice::ModeSense(uint8_t lun, scsi_mode_sense_6_data_t* out_data) {
  // CBW Configuration
  scsi_mode_sense_6_command_t command;
  memset(&command, 0, sizeof(command));
  command.opcode = UMS_MODE_SENSE6;
  command.page = 0x3F;  // all pages, current values
  command.allocation_length = sizeof(*out_data);

  SendCbw(lun, sizeof(*out_data), USB_DIR_IN, sizeof(command), &command);

  // read mode sense response
  QueueRead(sizeof(*out_data));

  zx_status_t status = ReadCsw(NULL);
  if (status == ZX_OK) {
    usb_request_copy_from(data_req_, out_data, sizeof(*out_data), 0);
  }
  return status;
}

zx_status_t UsbMassStorageDevice::DataTransfer(Transaction* txn, zx_off_t offset, size_t length,
                                               uint8_t ep_address) {
  usb_request_t* req = data_transfer_req_;

  zx_status_t status = usb_request_init(req, txn->op.rw.vmo, offset, length, ep_address);
  if (status != ZX_OK) {
    return status;
  }

  sync_completion_t completion;
  usb_request_complete_t complete = {
      .callback = ReqComplete,
      .ctx = &completion,
  };
  RequestQueue(req, &complete);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  status = req->response.status;
  if (status == ZX_OK && req->response.actual != length) {
    status = ZX_ERR_IO;
  }

  usb_request_release(req);
  return status;
}

zx_status_t UsbMassStorageDevice::Read(UmsBlockDevice* dev, Transaction* txn) {
  const auto& params = dev->GetBlockDeviceParameters();
  zx_off_t block_offset = txn->op.rw.offset_dev;
  uint32_t num_blocks = txn->op.rw.length;
  if ((block_offset >= params.total_blocks) ||
      ((params.total_blocks - block_offset) < num_blocks)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  size_t block_size = params.block_size;
  zx_off_t vmo_offset = txn->op.rw.offset_vmo * block_size;
  size_t max_blocks = max_transfer_ / block_size;
  zx_status_t status = ZX_OK;
  while (status == ZX_OK && num_blocks > 0) {
    size_t blocks = num_blocks;
    if (blocks > max_blocks) {
      blocks = max_blocks;
    }
    size_t length = blocks * block_size;
    // CBW Configuration
    // Need to use UMS_READ16 if block addresses are greater than 32 bit
    if (params.total_blocks > UINT32_MAX) {
      scsi_command16_t command;
      memset(&command, 0, sizeof(command));
      command.opcode = UMS_READ16;
      command.lba = htobe64(block_offset);
      command.length = htobe32(static_cast<uint32_t>(blocks));
      SendCbw(params.lun, static_cast<uint32_t>(length), USB_DIR_IN, sizeof(command), &command);
    } else if (blocks <= UINT16_MAX) {
      scsi_command10_t command;
      memset(&command, 0, sizeof(command));
      command.opcode = UMS_READ10;
      command.lba = htobe32(static_cast<uint32_t>(block_offset));
      command.length_hi = static_cast<uint8_t>(blocks >> 8);
      command.length_lo = static_cast<uint8_t>(blocks & 0xFF);
      SendCbw(params.lun, static_cast<uint32_t>(length), USB_DIR_IN, sizeof(command), &command);
    } else {
      scsi_command12_t command;
      memset(&command, 0, sizeof(command));
      command.opcode = UMS_READ12;
      command.lba = htobe32(static_cast<uint32_t>(block_offset));
      command.length = htobe32(static_cast<uint32_t>(blocks));
      SendCbw(params.lun, static_cast<uint32_t>(length), USB_DIR_IN, sizeof(command), &command);
    }

    status = DataTransfer(txn, vmo_offset, length, bulk_in_addr_);

    block_offset += blocks;
    num_blocks -= static_cast<uint32_t>(blocks);
    vmo_offset += (blocks * block_size);

    // receive CSW
    uint32_t residue;
    status = ReadCsw(&residue);
    if (status == ZX_OK && residue) {
      zxlogf(ERROR, "unexpected residue in Read");
      status = ZX_ERR_IO;
    }
  }

  return status;
}

zx_status_t UsbMassStorageDevice::Write(UmsBlockDevice* dev, Transaction* txn) {
  const auto& params = dev->GetBlockDeviceParameters();
  zx_off_t block_offset = txn->op.rw.offset_dev;
  uint32_t num_blocks = txn->op.rw.length;
  if ((block_offset >= params.total_blocks) ||
      ((params.total_blocks - block_offset) < num_blocks)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  size_t block_size = params.block_size;
  zx_off_t vmo_offset = txn->op.rw.offset_vmo * block_size;
  size_t max_blocks = max_transfer_ / block_size;
  zx_status_t status = ZX_OK;

  while (status == ZX_OK && num_blocks > 0) {
    size_t blocks = num_blocks;
    if (blocks > max_blocks) {
      blocks = max_blocks;
    }
    size_t length = blocks * block_size;

    // CBW Configuration
    // Need to use UMS_WRITE16 if block addresses are greater than 32 bit
    if (params.total_blocks > UINT32_MAX) {
      scsi_command16_t command;
      memset(&command, 0, sizeof(command));
      command.opcode = UMS_WRITE16;
      command.lba = htobe64(block_offset);
      command.length = htobe32(static_cast<uint32_t>(blocks));
      SendCbw(params.lun, static_cast<uint32_t>(length), USB_DIR_OUT, sizeof(command), &command);
    } else if (blocks <= UINT16_MAX) {
      scsi_command10_t command;
      memset(&command, 0, sizeof(command));
      command.opcode = UMS_WRITE10;
      command.lba = htobe32(static_cast<uint32_t>(block_offset));
      command.length_hi = static_cast<uint8_t>(static_cast<uint32_t>(blocks) >> 8);
      command.length_lo = static_cast<uint8_t>(static_cast<uint32_t>(blocks) & 0xFF);
      SendCbw(params.lun, static_cast<uint32_t>(length), USB_DIR_OUT, sizeof(command), &command);
    } else {
      scsi_command12_t command;
      memset(&command, 0, sizeof(command));
      command.opcode = UMS_WRITE12;
      command.lba = htobe32(static_cast<uint32_t>(block_offset));
      command.length = htobe32(static_cast<uint32_t>(blocks));
      SendCbw(params.lun, static_cast<uint32_t>(length), USB_DIR_OUT, sizeof(command), &command);
    }

    status = DataTransfer(txn, vmo_offset, length, bulk_out_addr_);

    block_offset += blocks;
    num_blocks -= static_cast<uint32_t>(blocks);
    vmo_offset += (blocks * block_size);

    // receive CSW
    uint32_t residue;
    status = ReadCsw(&residue);
    if (status == ZX_OK && residue) {
      zxlogf(ERROR, "unexpected residue in Write");
      status = ZX_ERR_IO;
    }
  }

  return status;
}

zx_status_t UsbMassStorageDevice::AddBlockDevice(fbl::RefPtr<UmsBlockDevice> dev) {
  BlockDeviceParameters params = dev->GetBlockDeviceParameters();
  uint8_t lun = params.lun;

  scsi_read_capacity_10_t data;
  zx_status_t status = ReadCapacity(lun, &data);
  if (status < 0) {
    zxlogf(ERROR, "read_capacity10 failed: %d", status);
    return status;
  }

  params.total_blocks = betoh32(data.lba);
  params.block_size = betoh32(data.block_length);

  if (params.total_blocks == 0xFFFFFFFF) {
    scsi_read_capacity_16_t data;
    status = ReadCapacity(lun, &data);
    if (status < 0) {
      zxlogf(ERROR, "read_capacity16 failed: %d", status);
      return status;
    }

    params.total_blocks = betoh64(data.lba);
    params.block_size = betoh32(data.block_length);
  }
  if (params.block_size == 0) {
    zxlogf(ERROR, "UMS zero block size");
    return ZX_ERR_INVALID_ARGS;
  }

  // +1 because this returns the address of the final block, and blocks are zero indexed
  params.total_blocks++;
  params.max_transfer = static_cast<uint32_t>(max_transfer_);
  dev->SetBlockDeviceParameters(params);
  // determine if LUN is read-only
  scsi_mode_sense_6_data_t ms_data;
  status = ModeSense(lun, &ms_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ModeSense failed: %d", status);
    return status;
  }
  unsigned char cache_sense[20];
  status = ModeSense(lun, 0x08, cache_sense, sizeof(cache_sense));
  params = dev->GetBlockDeviceParameters();
  if (status != ZX_OK) {
    zxlogf(WARN, "CacheSense failed: %d", status);
    params.cache_enabled = true;
  } else {
    params.cache_enabled = cache_sense[6] & (1 << 2);
  }

  if (ms_data.device_specific_param & MODE_SENSE_DSP_RO) {
    params.flags |= BLOCK_FLAG_READONLY;
  } else {
    params.flags &= ~BLOCK_FLAG_READONLY;
  }

  DEBUG_PRINT(("UMS: block size is: 0x%08x\n", params.block_size));
  DEBUG_PRINT(("UMS: total blocks is: %" PRId64 "\n", params.total_blocks));
  DEBUG_PRINT(("UMS: total size is: %" PRId64 "\n", params.total_blocks * params.block_size));
  DEBUG_PRINT(("UMS: read-only: %d removable: %d\n", !!(params.flags & BLOCK_FLAG_READONLY),
               !!(params.flags & BLOCK_FLAG_REMOVABLE)));
  dev->SetBlockDeviceParameters(params);
  return dev->Add();
}

zx_status_t UsbMassStorageDevice::CheckLunsReady() {
  zx_status_t status = ZX_OK;
  for (uint8_t lun = 0; lun <= max_lun_ && status == ZX_OK; lun++) {
    auto dev = block_devs_[lun];
    bool ready = false;

    status = TestUnitReady(lun);
    if (status == ZX_OK) {
      ready = true;
    }
    if (status == ZX_ERR_BAD_STATE) {
      ready = false;
      // command returned CSW_FAILED. device is there but media is not ready.
      uint8_t request_sense_data[UMS_REQUEST_SENSE_TRANSFER_LENGTH];
      status = RequestSense(lun, request_sense_data);
    }
    if (status != ZX_OK) {
      break;
    }
    BlockDeviceParameters params = dev->GetBlockDeviceParameters();
    if (ready && !params.device_added) {
      // this will set UmsBlockDevice.device_added if it succeeds
      status = AddBlockDevice(dev);
      params = dev->GetBlockDeviceParameters();
      if (status == ZX_OK) {
        params.device_added = true;
      } else {
        zxlogf(ERROR, "UMS: device_add for block device failed %d", status);
      }
    } else if (!ready && params.device_added) {
      dev->DdkRemoveDeprecated();
      params = dev->GetBlockDeviceParameters();
      params.device_added = false;
    }
    dev->SetBlockDeviceParameters(params);
  }

  return status;
}

int UsbMassStorageDevice::WorkerThread() {
  zx_status_t status = ZX_OK;
  for (uint8_t lun = 0; lun <= max_lun_; lun++) {
    uint8_t inquiry_data[UMS_INQUIRY_TRANSFER_LENGTH];
    status = Inquiry(lun, inquiry_data);
    if (status < 0) {
      zxlogf(ERROR, "Inquiry failed for lun %d status: %d", lun, status);
      DdkRemoveDeprecated();
      return status;
    }
    uint8_t rmb = inquiry_data[1] & 0x80;  // Removable Media Bit
    if (rmb) {
      BlockDeviceParameters params = block_devs_[lun]->GetBlockDeviceParameters();
      params.flags |= BLOCK_FLAG_REMOVABLE;
      block_devs_[lun]->SetBlockDeviceParameters(params);
    }
  }

  DdkMakeVisible();
  bool wait = true;
  if (CheckLunsReady() != ZX_OK) {
    return status;
  }

  ums::Transaction* current_txn = nullptr;
  while (1) {
    if (wait) {
#ifndef UNITTEST
      status = sync_completion_wait(&txn_completion_, ZX_SEC(1));
#else
      status = sync_completion_wait(&txn_completion_, ZX_SEC(0));
#endif
      if (list_is_empty(&queued_txns_) && !dead_) {
        if (CheckLunsReady() != ZX_OK) {
          return status;
        }
        continue;
      }
      sync_completion_reset(&txn_completion_);
    }
    Transaction* txn = nullptr;
    {
      fbl::AutoLock l(&txn_lock_);
      if (dead_) {
        break;
      }
      txn = list_remove_head_type(&queued_txns_, Transaction, node);
      if (txn == NULL) {
        wait = true;
        continue;
      } else {
        wait = false;
      }
      current_txn = txn;
    }
    zxlogf(DEBUG, "UMS PROCESS (%p)", &txn->op);

    UmsBlockDevice* dev = txn->dev;
    const auto& params = dev->GetBlockDeviceParameters();
    zx_status_t status;
    switch (txn->op.command & BLOCK_OP_MASK) {
      case BLOCK_OP_READ:
        if ((status = Read(dev, txn)) != ZX_OK) {
          zxlogf(ERROR, "ums: read of %u @ %zu failed: %d", txn->op.rw.length,
                 txn->op.rw.offset_dev, status);
        }
        break;
      case BLOCK_OP_WRITE:
        if ((status = Write(dev, txn)) != ZX_OK) {
          zxlogf(ERROR, "ums: write of %u @ %zu failed: %d", txn->op.rw.length,
                 txn->op.rw.offset_dev, status);
        }
        break;
      case BLOCK_OP_FLUSH:
        if (params.cache_enabled) {
          scsi_command10_t command;
          memset(&command, 0, sizeof(command));
          command.opcode = UMS_SYNCHRONIZE_CACHE;
          command.misc = 0;
          const auto& params = dev->GetBlockDeviceParameters();
          SendCbw(params.lun, 0, USB_DIR_OUT, sizeof(command), &command);
          uint32_t residue;
          status = ReadCsw(&residue);
          if (status == ZX_OK && residue) {
            zxlogf(ERROR, "unexpected residue in Write");
            status = ZX_ERR_IO;
          }
        } else {
          status = ZX_OK;
        }
        break;
      default:
        status = ZX_ERR_INVALID_ARGS;
        break;
    }
    {
      fbl::AutoLock l(&txn_lock_);
      if (current_txn == txn) {
        txn->Complete(status);
        current_txn = nullptr;
      }
    }
  }

  // complete any pending txns
  list_node_t txns = LIST_INITIAL_VALUE(txns);
  {
    fbl::AutoLock l(&txn_lock_);
    list_move(&queued_txns_, &txns);
  }

  Transaction* txn;
  while ((txn = list_remove_head_type(&queued_txns_, Transaction, node)) != NULL) {
    switch (txn->op.command & BLOCK_OP_MASK) {
      case BLOCK_OP_READ:
        zxlogf(ERROR, "ums: read of %u @ %zu discarded during unbind", txn->op.rw.length,
               txn->op.rw.offset_dev);
        break;
      case BLOCK_OP_WRITE:
        zxlogf(ERROR, "ums: write of %u @ %zu discarded during unbind", txn->op.rw.length,
               txn->op.rw.offset_dev);
        break;
    }
    txn->Complete(ZX_ERR_IO_NOT_PRESENT);
  }

  return ZX_OK;
}

static zx_status_t bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker checker;
  UsbMassStorageDevice* device(new (&checker) UsbMassStorageDevice(parent));
  if (!checker.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device->Init();
  return status;
}
static constexpr zx_driver_ops_t usb_mass_storage_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = bind;
  return ops;
}();
}  // namespace ums
// clang-format off
ZIRCON_DRIVER_BEGIN(usb_mass_storage, ums::usb_mass_storage_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_MSC),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_SUBCLASS_MSC_SCSI),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, USB_PROTOCOL_MSC_BULK_ONLY),
ZIRCON_DRIVER_END(usb_mass_storage)
    // clang-format on
