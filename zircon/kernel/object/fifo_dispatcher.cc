// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/fifo_dispatcher.h"

#include <lib/counters.h>
#include <string.h>
#include <zircon/rights.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>

KCOUNTER(dispatcher_fifo_create_count, "dispatcher.fifo.create")
KCOUNTER(dispatcher_fifo_destroy_count, "dispatcher.fifo.destroy")

// static
zx_status_t FifoDispatcher::Create(size_t count, size_t elemsize, uint32_t options,
                                   KernelHandle<FifoDispatcher>* handle0,
                                   KernelHandle<FifoDispatcher>* handle1, zx_rights_t* rights) {
  // count and elemsize must be nonzero
  // total size must be <= kMaxSizeBytes
  if (!count || !elemsize || (count > kMaxSizeBytes) || (elemsize > kMaxSizeBytes) ||
      ((count * elemsize) > kMaxSizeBytes)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AllocChecker ac;
  auto holder0 = fbl::AdoptRef(new (&ac) PeerHolder<FifoDispatcher>());
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;
  auto holder1 = holder0;

  auto data0 = ktl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[count * elemsize]);
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  KernelHandle fifo0(fbl::AdoptRef(
      new (&ac) FifoDispatcher(ktl::move(holder0), options, static_cast<uint32_t>(count),
                               static_cast<uint32_t>(elemsize), ktl::move(data0))));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  auto data1 = ktl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[count * elemsize]);
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  KernelHandle fifo1(fbl::AdoptRef(
      new (&ac) FifoDispatcher(ktl::move(holder1), options, static_cast<uint32_t>(count),
                               static_cast<uint32_t>(elemsize), ktl::move(data1))));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  fifo0.dispatcher()->InitPeer(fifo1.dispatcher());
  fifo1.dispatcher()->InitPeer(fifo0.dispatcher());

  *rights = default_rights();
  *handle0 = ktl::move(fifo0);
  *handle1 = ktl::move(fifo1);
  return ZX_OK;
}

FifoDispatcher::FifoDispatcher(fbl::RefPtr<PeerHolder<FifoDispatcher>> holder, uint32_t /*options*/,
                               uint32_t count, uint32_t elem_size, ktl::unique_ptr<uint8_t[]> data)
    : PeeredDispatcher(ktl::move(holder), ZX_FIFO_WRITABLE),
      elem_count_(count),
      elem_size_(elem_size),
      head_(0u),
      tail_(0u),
      data_(ktl::move(data)) {
  kcounter_add(dispatcher_fifo_create_count, 1);
}

FifoDispatcher::~FifoDispatcher() { kcounter_add(dispatcher_fifo_destroy_count, 1); }

void FifoDispatcher::on_zero_handles_locked() { canary_.Assert(); }

void FifoDispatcher::OnPeerZeroHandlesLocked() {
  canary_.Assert();

  UpdateStateLocked(ZX_FIFO_WRITABLE, ZX_FIFO_PEER_CLOSED);
}

zx_status_t FifoDispatcher::WriteFromUser(size_t elem_size, user_in_ptr<const uint8_t> ptr,
                                          size_t count, size_t* actual) {
  canary_.Assert();

  while (true) {
    ktl::variant<zx_status_t, UserCopyCaptureFaultsResult> write_result;
    {
      Guard<CriticalMutex> guard{get_lock()};
      if (!peer()) {
        return ZX_ERR_PEER_CLOSED;
      }
      AssertHeld(*peer()->get_lock());
      write_result = peer()->WriteSelfLocked(elem_size, ptr, count, actual);
    }
    // Check for any regular error and return it.
    if (ktl::holds_alternative<zx_status_t>(write_result)) {
      return ktl::get<zx_status_t>(write_result);
    }
    // Copy failed, need to check for and handle any page faults.
    UserCopyCaptureFaultsResult copy_result = ktl::get<UserCopyCaptureFaultsResult>(write_result);
    zx_status_t result = copy_result.status;
    if (auto fault = copy_result.fault_info) {
      // If we have a fault the original status is irrelevant and we replace it with the result of
      // the fault.
      result = Thread::Current::Get()->aspace()->SoftFault(fault->pf_va, fault->pf_flags);
    } else {
      // If there's no fault information then the assumption is that the original copy cannot have
      // succeeded.
      DEBUG_ASSERT(result != ZX_OK);
    }
    if (result != ZX_OK) {
      // Regardless of why the copy or fault failed it means the underlying pointer is somehow bad,
      // which we report to the user as an invalid argument.
      return ZX_ERR_INVALID_ARGS;
    }
  }
}

ktl::variant<zx_status_t, UserCopyCaptureFaultsResult> FifoDispatcher::WriteSelfLocked(
    size_t elem_size, user_in_ptr<const uint8_t> ptr, size_t count, size_t* actual) {
  canary_.Assert();

  if (elem_size != elem_size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (count == 0) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint32_t old_head = head_;

  // total number of available empty slots in the fifo
  size_t avail = elem_count_ - (head_ - tail_);

  if (avail == 0) {
    return ZX_ERR_SHOULD_WAIT;
  }

  bool was_empty = (avail == elem_count_);

  if (count > avail) {
    count = avail;
  }

  while (count > 0) {
    uint32_t offset = (head_ % elem_count_);

    // number of slots from target to end, inclusive
    uint32_t n = elem_count_ - offset;

    // number of slots we can actually copy
    size_t to_copy = (count > n) ? n : count;

    UserCopyCaptureFaultsResult result =
        ptr.copy_array_from_user_capture_faults(&data_[offset * elem_size_], to_copy * elem_size_);
    if (result.status != ZX_OK) {
      // roll back, in case this is the second copy
      head_ = old_head;
      return result;
    }

    // adjust head and count
    // due to size limitations on fifo, to_copy will always fit in a u32
    head_ += static_cast<uint32_t>(to_copy);
    count -= to_copy;
    ptr = ptr.byte_offset(to_copy * elem_size_);
  }

  // if was empty, we've become readable
  if (was_empty) {
    UpdateStateLocked(0u, ZX_FIFO_READABLE);
  }

  // if now full, we're no longer writable
  if (elem_count_ == (head_ - tail_)) {
    AssertHeld(*peer()->get_lock());
    peer()->UpdateStateLocked(ZX_FIFO_WRITABLE, 0u);
  }

  *actual = (head_ - old_head);
  return ZX_OK;
}

zx_status_t FifoDispatcher::ReadToUser(size_t elem_size, user_out_ptr<uint8_t> ptr, size_t count,
                                       size_t* actual) {
  canary_.Assert();
  while (true) {
    ktl::variant<zx_status_t, UserCopyCaptureFaultsResult> read_result;
    {
      Guard<CriticalMutex> guard{get_lock()};
      read_result = ReadToUserLocked(elem_size, ptr, count, actual);
    }
    // Check for any regular error and return it.
    if (ktl::holds_alternative<zx_status_t>(read_result)) {
      return ktl::get<zx_status_t>(read_result);
    }
    // Copy failed, need to check for and handle any page faults.
    UserCopyCaptureFaultsResult copy_result = ktl::get<UserCopyCaptureFaultsResult>(read_result);
    zx_status_t result = copy_result.status;
    if (auto fault = copy_result.fault_info) {
      // If we have a fault the original status is irrelevant and we replace it with the result of
      // the fault.
      result = Thread::Current::Get()->aspace()->SoftFault(fault->pf_va, fault->pf_flags);
    } else {
      // If there's no fault information then the assumption is that the original copy cannot have
      // succeeded.
      DEBUG_ASSERT(result != ZX_OK);
    }
    if (result != ZX_OK) {
      // Regardless of why the copy or fault failed it means the underlying pointer is somehow bad,
      // which we report to the user as an invalid argument.
      return ZX_ERR_INVALID_ARGS;
    }
  }
}

ktl::variant<zx_status_t, UserCopyCaptureFaultsResult> FifoDispatcher::ReadToUserLocked(
    size_t elem_size, user_out_ptr<uint8_t> ptr, size_t count, size_t* actual) {
  canary_.Assert();

  if (elem_size != elem_size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (count == 0) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint32_t old_tail = tail_;

  // total number of available entries to read from the fifo
  size_t avail = (head_ - tail_);

  if (avail == 0) {
    return peer() ? ZX_ERR_SHOULD_WAIT : ZX_ERR_PEER_CLOSED;
  }

  bool was_full = (avail == elem_count_);

  if (count > avail) {
    count = avail;
  }

  while (count > 0) {
    uint32_t offset = (tail_ % elem_count_);

    // number of slots from target to end, inclusive
    uint32_t n = elem_count_ - offset;

    // number of slots we can actually copy
    size_t to_copy = (count > n) ? n : count;

    UserCopyCaptureFaultsResult result =
        ptr.copy_array_to_user_capture_faults(&data_[offset * elem_size_], to_copy * elem_size_);
    if (result.status != ZX_OK) {
      // roll back, in case this is the second copy
      tail_ = old_tail;
      return result;
    }

    // adjust tail and count
    // due to size limitations on fifo, to_copy will always fit in a u32
    tail_ += static_cast<uint32_t>(to_copy);
    count -= to_copy;
    ptr = ptr.byte_offset(to_copy * elem_size_);
  }

  // if we were full, we have become writable
  if (was_full && peer()) {
    AssertHeld(*peer()->get_lock());
    peer()->UpdateStateLocked(0u, ZX_FIFO_WRITABLE);
  }

  // if we've become empty, we're no longer readable
  if ((head_ - tail_) == 0) {
    UpdateStateLocked(ZX_FIFO_READABLE, 0u);
  }

  *actual = (tail_ - old_tail);
  return ZX_OK;
}
