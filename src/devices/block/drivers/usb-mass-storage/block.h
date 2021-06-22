// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_USB_MASS_STORAGE_BLOCK_H_
#define SRC_DEVICES_BLOCK_DRIVERS_USB_MASS_STORAGE_BLOCK_H_

#include <fuchsia/hardware/block/cpp/banjo.h>
#include <stdint.h>

#include <ddktl/device.h>
#include <fbl/function.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "usb-mass-storage.h"

namespace ums {

class UsbMassStorageDevice;

struct BlockDeviceParameters {
  bool device_added = false;

  bool cache_enabled = false;

  uint8_t lun = 0;  // our logical unit number

  unsigned char padding = 0;

  uint32_t block_size = 0;

  uint32_t flags = 0;  // flags for block_info_t

  uint32_t max_transfer = 0;

  uint64_t total_blocks = 0;

  bool operator==(const BlockDeviceParameters& other) {
    return memcmp(this, &other, sizeof(other)) == 0;
  }
};
// This ensures that it is safe to use memcmp to compare equality
static_assert(std::has_unique_object_representations_v<BlockDeviceParameters>);

class UmsBlockDevice;
using DeviceType = ddk::Device<UmsBlockDevice, ddk::GetSizable>;
class UmsBlockDevice : public DeviceType,
                       public ddk::BlockImplProtocol<UmsBlockDevice, ddk::base_protocol>,
                       public fbl::RefCounted<UmsBlockDevice> {
 public:
  explicit UmsBlockDevice(zx_device_t* parent, uint8_t lun,
                          fbl::Function<void(ums::Transaction*)>&& queue_callback)
      : DeviceType(parent), queue_callback_(std::move(queue_callback)) {
    parameters_ = {};
    parameters_.lun = lun;
  }

  zx_status_t Add();

  // Device protocol implementation.
  zx_off_t DdkGetSize();

  void DdkRelease();

  // Block protocol implementation.
  void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);

  void BlockImplQueue(block_op_t* op, block_impl_queue_callback completion_cb, void* cookie);

  const BlockDeviceParameters& GetBlockDeviceParameters() { return parameters_; }

  void SetBlockDeviceParameters(const BlockDeviceParameters& parameters) {
    parameters_ = parameters;
  }

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(UmsBlockDevice);

 private:
  fbl::Function<void(ums::Transaction*)> queue_callback_;
  BlockDeviceParameters parameters_;
};
}  // namespace ums

#endif  // SRC_DEVICES_BLOCK_DRIVERS_USB_MASS_STORAGE_BLOCK_H_
