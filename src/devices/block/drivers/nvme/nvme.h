// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_NVME_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_NVME_H_

#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/cpp/banjo.h>

#include <ddktl/device.h>

namespace nvme {

struct nvme_device_t;

class Nvme;
using DeviceType = ddk::Device<Nvme, ddk::Initializable>;
class Nvme : public DeviceType, public ddk::BlockImplProtocol<Nvme, ddk::base_protocol> {
 public:
  explicit Nvme(zx_device_t* parent) : DeviceType(parent) {}
  ~Nvme() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t AddDevice(zx_device_t* dev);

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // BlockImpl implementations
  void BlockImplQuery(block_info_t* out_info, uint64_t* out_block_op_size);
  void BlockImplQueue(block_op_t* txn, block_impl_queue_callback callback, void* cookie);

 private:
  nvme_device_t* nvme_;
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_NVME_H_
