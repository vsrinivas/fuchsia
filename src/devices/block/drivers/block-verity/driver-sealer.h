// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DRIVER_SEALER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DRIVER_SEALER_H_

#include "device-info.h"
#include "sealer.h"

namespace block_verity {

class DriverSealer : public Sealer {
 public:
  DriverSealer(DeviceInfo info);
  ~DriverSealer();

  // Hooks sealing startup so we can allocate a VMO for block operations.
  zx_status_t StartSealing(void* cookie, sealer_callback callback) override;

  void RequestRead(uint64_t block) override;
  void OnReadCompleted(zx_status_t status, block_op_t* block);
  static void ReadCompletedCallback(void* cookie, zx_status_t status, block_op_t* block);

  void WriteIntegrityBlock(HashBlockAccumulator& hba, uint64_t block) override;
  void OnIntegrityWriteCompleted(zx_status_t status, block_op_t* block);
  static void IntegrityWriteCompletedCallback(void* cookie, zx_status_t status, block_op_t* block);

  void WriteSuperblock() override;
  void OnSuperblockWriteCompleted(zx_status_t status, block_op_t* block);
  static void SuperblockWriteCompletedCallback(void* cookie, zx_status_t status, block_op_t* block);

  void RequestFlush() override;
  void OnFlushCompleted(zx_status_t status, block_op_t* block);
  static void FlushCompletedCallback(void* cookie, zx_status_t status, block_op_t* block);

 private:
  // Drive geometry/block client handle.
  const DeviceInfo info_;

  // The number of outstanding block requests.  We can only safely terminate
  // once these are all settled.
  // For this first pass implementation, we never have more than 1 request
  // outstanding, so this should always be either 0 or 1.
  uint64_t outstanding_block_requests_;

  // A single block op request buffer, allocated to be the size of the parent
  // block op size request.
  std::unique_ptr<uint8_t[]> block_op_buf_;

  // A vmo used in block device operations
  zx::vmo block_op_vmo_;

  // The start address where that vmo is mapped
  uint8_t* vmo_base_;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DRIVER_SEALER_H_
