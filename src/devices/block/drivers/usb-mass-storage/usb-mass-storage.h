// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_USB_MASS_STORAGE_USB_MASS_STORAGE_H_
#define SRC_STORAGE_BLOCK_DRIVERS_USB_MASS_STORAGE_USB_MASS_STORAGE_H_

#include <inttypes.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl-async/bind.h>
#include <lib/sync/completion.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/ums.h>
#include <zircon/listnode.h>

#include <atomic>
#include <memory>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/usb.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/array.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

namespace ums {

class UmsBlockDevice;

// struct representing a block device for a logical unit
struct Transaction {
  void Complete(zx_status_t status) {
    zxlogf(DEBUG, "UMS DONE %d (%p)", status, &op);
    completion_cb(cookie, status, &op);
  }

  block_op_t op;

  block_impl_queue_callback completion_cb;

  void* cookie;

  list_node_t node;

  ums::UmsBlockDevice* dev;
};

class UsbMassStorageDevice;

struct UsbRequestContext {
  usb_request_complete_t completion;
};

using MassStorageDeviceType = ddk::Device<UsbMassStorageDevice, ddk::UnbindableDeprecated>;
class UsbMassStorageDevice : public MassStorageDeviceType {
 public:
  explicit UsbMassStorageDevice(zx_device_t* parent = nullptr) : MassStorageDeviceType(parent) {}

  ~UsbMassStorageDevice() {}

  void QueueTransaction(Transaction* txn);

  void DdkRelease();

  void DdkUnbindDeprecated();

  // Performs the object initialization.
  zx_status_t Init();

  DISALLOW_COPY_ASSIGN_AND_MOVE(UsbMassStorageDevice);

 private:
  zx_status_t Reset();

  void SendCbw(uint8_t lun, uint32_t transfer_length, uint8_t flags, uint8_t command_len,
               void* command);

  zx_status_t ReadCsw(uint32_t* out_residue);

  csw_status_t VerifyCsw(usb_request_t* csw_request, uint32_t* out_residue);

  void QueueRead(uint16_t transfer_length);

  zx_status_t Inquiry(uint8_t lun, uint8_t* out_data);

  zx_status_t TestUnitReady(uint8_t lun);

  zx_status_t RequestSense(uint8_t lun, uint8_t* out_data);

  zx_status_t ReadCapacity(uint8_t lun, scsi_read_capacity_10_t* out_data);

  zx_status_t ReadCapacity(uint8_t lun, scsi_read_capacity_16_t* out_data);

  zx_status_t ModeSense(uint8_t lun, scsi_mode_sense_6_data_t* out_data);

  zx_status_t ModeSense(uint8_t lun, uint8_t page, void* data, uint8_t transfer_length);

  zx_status_t DataTransfer(Transaction* txn, zx_off_t offset, size_t length, uint8_t ep_address);

  zx_status_t Read(ums::UmsBlockDevice* dev, Transaction* txn);
  zx_status_t Write(ums::UmsBlockDevice* dev, Transaction* txn);

  zx_status_t AddBlockDevice(fbl::RefPtr<ums::UmsBlockDevice> dev);

  zx_status_t CheckLunsReady();

  int WorkerThread();

  void RequestQueue(usb_request_t* request, const usb_request_complete_t* completion);

  usb::UsbDevice usb_;

  uint32_t tag_send_;  // next tag to send in CBW

  uint32_t tag_receive_;  // next tag we expect to receive in CSW

  uint8_t max_lun_;  // index of last logical unit

  size_t max_transfer_;  // maximum transfer size reported by usb_get_max_transfer_size()

  uint8_t interface_number_;

  uint8_t bulk_in_addr_;

  uint8_t bulk_out_addr_;

  size_t bulk_in_max_packet_;

  size_t bulk_out_max_packet_;

  usb_request_t* cbw_req_;

  usb_request_t* data_req_;

  usb_request_t* csw_req_;

  usb_request_t* data_transfer_req_;  // for use in DataTransfer

  size_t parent_req_size_;

  thrd_t worker_thread_;

  std::atomic_size_t pending_requests_ = 0;

  bool dead_;

  // list of queued transactions
  list_node_t queued_txns_;

  sync_completion_t txn_completion_;  // signals WorkerThread when new txns are available
                                      // and when device is dead
  fbl::Mutex txn_lock_;               // protects queued_txns, txn_completion and dead

  fbl::Array<fbl::RefPtr<ums::UmsBlockDevice>> block_devs_;
};
}  // namespace ums

#endif  // SRC_STORAGE_BLOCK_DRIVERS_USB_MASS_STORAGE_USB_MASS_STORAGE_H_
