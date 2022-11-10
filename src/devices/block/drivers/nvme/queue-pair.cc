// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme/queue-pair.h"

#include <lib/ddk/debug.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include "fbl/auto_lock.h"
#include "src/devices/block/drivers/nvme/commands.h"
#include "src/devices/block/drivers/nvme/commands/nvme-io.h"
#include "src/devices/block/drivers/nvme/registers.h"

namespace nvme {

zx::result<std::unique_ptr<QueuePair>> QueuePair::Create(zx::unowned_bti bti, size_t queue_id,
                                                         size_t max_entries, CapabilityReg& caps,
                                                         fdf::MmioBuffer& mmio, bool prealloc_prp) {
  auto completion_queue = Queue::Create(bti->borrow(), queue_id, max_entries, sizeof(Completion));
  if (completion_queue.is_error()) {
    return completion_queue.take_error();
  }

  auto submission_queue = Queue::Create(bti->borrow(), queue_id, max_entries, sizeof(Submission));
  if (submission_queue.is_error()) {
    return submission_queue.take_error();
  }

  auto completion_doorbell = DoorbellReg::CompletionQueue(queue_id, caps).FromValue(0);
  auto submission_doorbell = DoorbellReg::SubmissionQueue(queue_id, caps).FromValue(0);

  auto queue_pair =
      std::make_unique<QueuePair>(std::move(*completion_queue), std::move(*submission_queue),
                                  std::move(bti), mmio, completion_doorbell, submission_doorbell);
  if (prealloc_prp) {
    zx_status_t status = queue_pair->PreallocatePrpBuffers();
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }
  return zx::ok(std::move(queue_pair));
}

zx_status_t QueuePair::PreallocatePrpBuffers() {
  fbl::AutoLock txn_lock(&transaction_lock_);
  for (auto& txn_data : txns_) {
    zx_status_t status;
    status = txn_data.prp_buffer.Init(bti_->get(), zx_system_get_page_size(), IO_BUFFER_RW);
    if (status != ZX_OK) {
      return status;
    }
    status = txn_data.prp_buffer.PhysMap();
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t QueuePair::CheckForNewCompletion(IoCommand** io_cmd, bool* has_error_code) {
  fbl::AutoLock lock(&completion_lock_);
  if (static_cast<Completion*>(completion_.Peek())->phase() != completion_ready_phase_) {
    return ZX_ERR_SHOULD_WAIT;
  }

  Completion* comp = static_cast<Completion*>(completion_.Next());
  if (completion_.NextIndex() == 0) {
    // Toggle the ready phase when we're about to wrap around.
    completion_ready_phase_ ^= 1;
  }
  sq_head_.store(comp->sq_head());

  auto txn_id = comp->command_id();
  {
    fbl::AutoLock txn_lock(&transaction_lock_);
    if (txn_id > txns_.size()) {
      zxlogf(ERROR, "Completed transaction has invalid ID: %u", txn_id);
      return ZX_ERR_BAD_STATE;
    }
    TransactionData& txn_data = txns_[txn_id];
    if (!txn_data.active) {
      zxlogf(ERROR, "Completed transaction #%u was not active.", txn_id);
      return ZX_ERR_BAD_STATE;
    }

    *io_cmd = txn_data.io_cmd;
    if (txn_data.pmt.is_valid()) {
      zx_status_t status = txn_data.pmt.unpin();
      if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to unpin IO buffer: %s", zx_status_get_string(status));
        return ZX_ERR_INTERNAL;
      }
    }
    txn_data.ClearExceptPrp();
  }

  if (comp->status_code_type() == StatusCodeType::kGeneric && comp->status_code() == 0) {
    *has_error_code = false;
    zxlogf(TRACE, "Completed transaction #%u command %p OK.", txn_id, *io_cmd);
  } else {
    *has_error_code = true;
    zxlogf(ERROR, "Completed transaction #%u command %p ERROR: status type=%01x, status=%02x",
           txn_id, *io_cmd, comp->status_code_type(), comp->status_code());
  }
  return ZX_OK;
}

void QueuePair::RingCompletionDb() {
  // Ring the doorbell.
  // TODO(fxbug.dev/102133): Retire this lock, and document the class as thread-unsafe.
  fbl::AutoLock lock(&completion_lock_);
  completion_doorbell_.set_value(static_cast<uint32_t>(completion_.NextIndex())).WriteTo(&mmio_);
}

zx::result<> QueuePair::Submit(cpp20::span<uint8_t> submission_data,
                               std::optional<zx::unowned_vmo> data_vmo, zx_off_t vmo_offset,
                               size_t bytes, IoCommand* io_cmd) {
  fbl::AutoLock lock(&submission_lock_);
  if ((submission_.NextIndex() + 1) % submission_.entry_count() == sq_head_.load()) {
    // No room. Try again later.
    return zx::error(ZX_ERR_SHOULD_WAIT);
  }
  if (submission_data.size() != sizeof(Submission)) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  fbl::AutoLock txn_lock(&transaction_lock_);
  // Allocate a new submission
  size_t index = submission_.NextIndex();
  TransactionData& txn_data = txns_[index];
  if (txn_data.active) {
    // This should not happen.
    zxlogf(ERROR, "Trying to submit a new transaction but transaction %zd is already active",
           index);
    return zx::error(ZX_ERR_BAD_STATE);
  }
  txn_data.ClearExceptPrp();
  // We only peek here so that if the transaction setup fails somewhere we can easily roll-back
  Submission* submission = static_cast<Submission*>(submission_.Peek());
  // Copy provided data into place.
  memcpy(static_cast<void*>(submission), submission_data.data(), submission_data.size());

  // We do not support metadata.
  submission->metadata_pointer = 0;
  submission->set_cid(static_cast<uint32_t>(index)).set_fused(0).set_data_transfer_mode(0);

  zx_paddr_t* page_list = nullptr;
  size_t page_count = 0;
  if (io_cmd != nullptr) {
    txn_data.io_cmd = io_cmd;

    // Page offset of first page of transfer
    const size_t page_offset = vmo_offset & (~kPageMask);
    // Byte offset into first page of transfer
    const size_t byte_offset = vmo_offset & kPageMask;
    // Total pages mapped / touched
    page_count = (byte_offset + bytes + kPageMask) >> kPageShift;
    if (page_count > kMaxTransferPages) {
      zxlogf(ERROR, "Did not expect a single transaction to transfer more than %zd pages.",
             kMaxTransferPages);
      return zx::error(ZX_ERR_BAD_STATE);
    }

    if (!txn_data.prp_buffer.is_valid()) {
      zxlogf(ERROR, "No PRP buffer was preallocated for this IO transaction.");
      return zx::error(ZX_ERR_BAD_STATE);
    }
    page_list = static_cast<zx_paddr_t*>(txn_data.prp_buffer.virt());

    // Read disk and write memory (PERM_WRITE), or read memory (PERM_READ) and write disk.
    const uint32_t options = (submission->opcode() == NvmIoSubmission::kReadOpcode)
                                 ? ZX_BTI_PERM_WRITE
                                 : ZX_BTI_PERM_READ;
    // These get unpinned in CheckForNewCompletion().
    zx_status_t status = bti_->pin(options, *data_vmo.value(), page_offset,
                                   page_count << kPageShift, page_list, page_count, &txn_data.pmt);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to pin IO buffer: %s", zx_status_get_string(status));
      return zx::error(status);
    }

    page_list[0] += byte_offset;
    zxlogf(TRACE, "Submitting transaction #%zu command %p: op=%s, pages=%zu", index, io_cmd,
           options == ZX_BTI_PERM_WRITE ? "RD" : "WR", page_count);
  } else if (data_vmo.has_value()) {
    // TODO(fxbug.dev/102133): Merge this admin command codepath with the above IO admin codepath.
    // Map the VMO in.
    zx_status_t status =
        txn_data.buffer.InitVmo(bti_->get(), data_vmo.value()->get(), vmo_offset, IO_BUFFER_RW);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    status = txn_data.buffer.PhysMap();
    if (status != ZX_OK) {
      return zx::error(status);
    }

    page_count = txn_data.buffer.phys_count();
    page_list = const_cast<zx_paddr_t*>(txn_data.buffer.phys_list());
    page_list[0] += vmo_offset;
  }

  if (page_list != nullptr) {
    submission->data_pointer[0] = page_list[0];
    if (page_count == 2) {
      submission->data_pointer[1] = page_list[1];
    } else if (page_count > 2) {
      // See QueuePair::kMaxTransferPages.
      submission->data_pointer[1] = txn_data.prp_buffer.phys_list()[0] + sizeof(zx_paddr_t*);
    }
  }

  // We used Peek() before, so advance the pointer, and mark the transaction as in-flight.
  submission_.Next();
  txn_data.active = true;

  // Ring the doorbell.
  submission_doorbell_.set_value(static_cast<uint32_t>(submission_.NextIndex())).WriteTo(&mmio_);
  return zx::ok();
}

zx::result<> QueuePair::PreparePrpList(ddk::IoBuffer& buf, cpp20::span<const zx_paddr_t> pages) {
  const size_t addresses_per_page = zx_system_get_page_size() / sizeof(zx_paddr_t);
  size_t page_count = 0;
  // TODO(fxbug.dev/102133): improve this in cases where we would allocate a page with only one
  // entry.
  page_count = pages.size() / (addresses_per_page - 1);
  page_count += 1;

  zx_status_t status = buf.Init(bti_->get(), page_count * zx_system_get_page_size(), IO_BUFFER_RW);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  status = buf.PhysMap();
  if (status != ZX_OK) {
    return zx::error(status);
  }

  zx_paddr_t* addresses = static_cast<zx_paddr_t*>(buf.virt());
  size_t prp_index = 0;
  const zx_paddr_t* prp_list_pages = buf.phys_list();
  size_t prp_list_page_count = buf.phys_count();
  for (size_t i = 0; i < pages.size(); i++, prp_index++) {
    // If we're about to cross a page boundary, put the address of the next page here.
    if (prp_index % addresses_per_page == (addresses_per_page - 1)) {
      if (prp_list_page_count == 0) {
        zxlogf(ERROR, "Ran out of PRP pages?");
        return zx::error(ZX_ERR_INTERNAL);
      }
      addresses[prp_index] = *prp_list_pages;
      prp_list_pages++;
      prp_list_page_count--;
      prp_index++;
    }

    addresses[prp_index] = pages[i];
  }

  return zx::ok();
}

}  // namespace nvme
