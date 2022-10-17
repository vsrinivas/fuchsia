// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_QUEUE_PAIR_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_QUEUE_PAIR_H_

#include <lib/ddk/io-buffer.h>
#include <lib/fpromise/bridge.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/stdcompat/span.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <fbl/mutex.h>

#include "src/devices/block/drivers/nvme-cpp/commands.h"
#include "src/devices/block/drivers/nvme-cpp/queue.h"
#include "src/devices/block/drivers/nvme-cpp/registers.h"
namespace nvme {

// Data associated with a transaction.
struct TransactionData {
  using Completer = fpromise::completer<Completion, Completion>;
  // Promise completer.
  Completer completer;
  // Data buffer, provided by the user.
  ddk::IoBuffer buffer;
  // If the first buffer covers more than two pages, this buffer
  // will be allocated by |QueuePair::Submit| and will contain a PRP list, as described
  // by NVM Express Base Specification 2.0 Section 4.1.1, "Physical Region Page Entry and List"
  ddk::IoBuffer prp_buffer;
  // Set to true when a transaction is submitted, and set to false when it is completed.
  bool active = false;
};

// A QueuePair represents a completion and submission queue that are paired together.
// It manages the relationship between the two.
// While the spec allows many submission queues to map to one completion queue, for simplicity
// we always assume there is a 1:1 relationship between the two.
class QueuePair {
 public:
  // Prefer |QueuePair::Create|.
  QueuePair(Queue completion, Queue submission, zx::unowned_bti bti, fdf::MmioBuffer& mmio,
            DoorbellReg completion_doorbell, DoorbellReg submission_doorbell)
      : completion_(std::move(completion)),
        submission_(std::move(submission)),
        txns_(submission_.entry_count()),
        bti_(std::move(bti)),
        mmio_(mmio),
        completion_doorbell_(completion_doorbell),
        submission_doorbell_(submission_doorbell) {}

  static zx::result<std::unique_ptr<QueuePair>> Create(zx::unowned_bti bti, size_t queue_id,
                                                       size_t max_entries, CapabilityReg& reg,
                                                       fdf::MmioBuffer& mmio);

  const Queue& completion() { return completion_; }
  const Queue& submission() { return submission_; }
  const std::vector<TransactionData>& txn_data() { return txns_; }

  // Check the completion queue for any new completed elements. Should be called from an async task
  // posted by the interrupt handler.
  void CheckForNewCompletions();

  // Submit will take ownership of |completer| only if submission succeeds. If submission fails, it
  // is up to the caller to appropriately fail the completer.
  zx::result<> Submit(Submission& submission, std::optional<zx::unowned_vmo> data,
                      zx_off_t vmo_offset, TransactionData::Completer& completer) {
    return Submit(cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&submission), sizeof(submission)),
                  std::move(data), vmo_offset, completer);
  }

 private:
  friend class QueuePairTest;

  // Raw implementation of submit that operates on a byte span rather than a submission.
  zx::result<> Submit(cpp20::span<uint8_t> submission, std::optional<zx::unowned_vmo> data,
                      zx_off_t vmo_offset, TransactionData::Completer& completer);

  // Puts a PRP list in |buf| containing the given addresses.
  zx::result<> PreparePrpList(ddk::IoBuffer& buf, cpp20::span<const zx_paddr_t> pages);

  // Completion queue.
  Queue completion_ __TA_GUARDED(completion_lock_);
  // Submission queue.
  Queue submission_ __TA_GUARDED(submission_lock_);
  // This is an array of data associated with each transaction.
  // Each transaction's ID is equal to its index in the queue, and this array works the same way.
  std::vector<TransactionData> txns_ __TA_GUARDED(transaction_lock_);
  // Entries in the completion queue with phase equal to this are done.
  uint8_t completion_ready_phase_ __TA_GUARDED(completion_lock_) = 1;
  // Last position the controller reported it was up to in the submission queue.
  std::atomic_size_t sq_head_ = submission_.entry_count() - 1;

  fbl::Mutex submission_lock_;
  fbl::Mutex completion_lock_;
  // We always acquire transaction_lock_ after submission/completion lock.
  fbl::Mutex transaction_lock_;

  zx::unowned_bti bti_;

  fdf::MmioBuffer& mmio_;
  DoorbellReg completion_doorbell_;
  DoorbellReg submission_doorbell_;
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_QUEUE_PAIR_H_
