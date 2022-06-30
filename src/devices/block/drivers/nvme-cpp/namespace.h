// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_NAMESPACE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_NAMESPACE_H_

#include <fuchsia/hardware/block/cpp/banjo.h>

#include <ddktl/device.h>

namespace nvme {

class Nvme;

class Namespace;
using NamespaceDeviceType = ddk::Device<Namespace, ddk::Initializable>;
class Namespace : public NamespaceDeviceType,
                  public ddk::BlockImplProtocol<Namespace, ddk::base_protocol> {
 public:
  explicit Namespace(zx_device_t* parent, Nvme* controller, uint32_t id)
      : NamespaceDeviceType(parent), controller_(controller), namespace_id_(id) {}

  // Create a namespace on |controller| with |id|.
  static zx_status_t Create(Nvme* controller, uint32_t id);
  zx_status_t Bind();

  // BlockImpl implementations
  void BlockImplQuery(block_info_t* out_info, uint64_t* out_block_op_size);
  void BlockImplQueue(block_op_t* txn, block_impl_queue_callback callback, void* cookie);

  uint32_t id() const { return namespace_id_; }

  void DdkRelease() { delete this; }
  void DdkInit(ddk::InitTxn txn);

 private:
  Nvme* controller_;
  const uint32_t namespace_id_;
  uint32_t lba_size_;
  uint64_t lba_count_;
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_NAMESPACE_H_
