// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_NAND_NAND_H_
#define SRC_DEVICES_NAND_DRIVERS_NAND_NAND_H_

#include <fuchsia/hardware/nand/cpp/banjo.h>
#include <fuchsia/hardware/rawnand/cpp/banjo.h>
#include <lib/ddk/driver.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/operation/nand.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>
#include <zircon/types.h>

#include <ddktl/device.h>
#include <fbl/condition_variable.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "lib/inspect/cpp/vmo/types.h"
#include "src/devices/nand/drivers/nand/read_cache.h"

namespace nand {

using Transaction = nand::BorrowedOperation<>;

class NandDevice;
using DeviceType = ddk::Device<NandDevice, ddk::GetSizable, ddk::Suspendable>;

class NandDevice : public DeviceType, public ddk::NandProtocol<NandDevice, ddk::base_protocol> {
 public:
  // If we're going to experience device level failures that result in data loss
  // or corruption, let's be very sure.
  static constexpr size_t kNandReadRetries = 100;

  explicit NandDevice(zx_device_t* parent) : DeviceType(parent), raw_nand_(parent) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(NandDevice);

  ~NandDevice();

  static zx_status_t Create(void* ctx, zx_device_t* parent);
  zx_status_t Bind();
  zx_status_t Init();

  // Device protocol implementation.
  zx_off_t DdkGetSize() { return device_get_size(parent()); }
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkRelease();

  // Nand protocol implementation.
  void NandQuery(nand_info_t* info_out, size_t* nand_op_size_out);
  void NandQueue(nand_operation_t* op, nand_queue_callback completion_cb, void* cookie);
  zx_status_t NandGetFactoryBadBlockList(uint32_t* bad_blocks, size_t bad_block_len,
                                         size_t* num_bad_blocks);

  zx::vmo GetDuplicateInspectVmoForTest() const;

 private:
  // Maps the data and oob vmos from the specified |nand_op| into memory.
  zx_status_t MapVmos(const nand_operation_t& nand_op, fzl::VmoMapper* data, uint8_t** vaddr_data,
                      fzl::VmoMapper* oob, uint8_t** vaddr_oob);

  // Calls controller specific read function.
  // data, oob: pointers to user out-of-band data and data buffers.
  // nand_page : NAND page address to read.
  // ecc_correct : Number of ecc corrected bitflips (< 0 indicates
  // ecc could not correct all bitflips - caller needs to check that).
  // retries : Retry logic may not be needed.
  zx_status_t ReadPage(uint8_t* data, uint8_t* oob, uint32_t nand_page, uint32_t* corrected_bits,
                       size_t retries);

  zx_status_t EraseOp(nand_operation_t* nand_op);
  zx_status_t ReadOp(nand_operation_t* nand_op);
  zx_status_t WriteOp(nand_operation_t* nand_op);

  void DoIo(Transaction txn);
  zx_status_t WorkerThread();

  void Shutdown();

  ddk::RawNandProtocolClient raw_nand_;

  nand_info_t nand_info_;
  uint32_t num_nand_pages_;

  inspect::Inspector inspect_;
  inspect::Node root_;

  // Track number of bit flips in each read attempt, ECC failures records max ECC plus one.
  inspect::LinearUintHistogram read_ecc_bit_flips_;

  // Number of read attempts until success. Failures will populate as maxint to go in the overflow
  // bucket.
  inspect::ExponentialUintHistogram read_attempts_;

  // Count internal read failures
  inspect::UintProperty read_internal_failure_;

  // Count read failures where all retries are exhausted.
  inspect::UintProperty read_failure_;

  // Cache for recent reads that came close to failure.
  std::unique_ptr<ReadCache> dangerous_reads_cache_ = nullptr;

  // If a read call doesn't want the oob, store it here instead to facilitate caching.
  std::unique_ptr<uint8_t[]> oob_buffer_ = nullptr;

  thrd_t worker_thread_;

  fbl::Mutex lock_;
  nand::BorrowedOperationQueue<> txn_queue_ TA_GUARDED(lock_);
  fbl::ConditionVariable worker_event_ TA_GUARDED(lock_);
  bool shutdown_ TA_GUARDED(lock_) = false;
};

}  // namespace nand

#endif  // SRC_DEVICES_NAND_DRIVERS_NAND_NAND_H_
