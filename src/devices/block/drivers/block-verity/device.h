// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEVICE_H_

#include <zircon/device/block.h>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/mutex.h>

#include "src/devices/block/drivers/block-verity/device-info.h"

namespace block_verity {

// See ddk::Device in ddktl/device.h
class Device;
using DeviceType = ddk::Device<Device, ddk::GetProtocolable, ddk::GetSizable, ddk::Unbindable>;

class Device : public DeviceType, public ddk::BlockImplProtocol<Device, ddk::base_protocol> {
 public:
  Device(zx_device_t* parent, DeviceInfo&& info);

  // Disallow copy, assign, and move.
  Device(const Device&) = delete;
  Device(Device&&) = delete;
  Device& operator=(const Device&) = delete;
  Device& operator=(Device&&) = delete;

  ~Device() = default;

  uint64_t op_size() { return info_.op_size; }

  // ddk::Device methods; see ddktl/device.h
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  zx_off_t DdkGetSize();
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // ddk::BlockProtocol methods; see ddktl/protocol/block.h
  void BlockImplQuery(block_info_t* out_info, size_t* out_op_size);
  void BlockImplQueue(block_op_t* block_op, block_impl_queue_callback completion_cb, void* cookie)
      __TA_EXCLUDES(mtx_);

  // The callback that we give to the underlying block device when we queue
  // operations against it.  It simply translates block offsets back and completes the
  // matched block requests.
  static void BlockCallback(void* cookie, zx_status_t status, block_op_t* block);

  // Completes the block operation by calling the appropriate callback with the
  // appropriate status.
  void BlockComplete(block_op_t* block, zx_status_t status);

 private:
  fbl::Mutex mtx_;

  // Device configuration, as provided by the DeviceManager at creation. Its
  // constness allows it to be used without holding the lock.
  const DeviceInfo info_;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEVICE_H_
