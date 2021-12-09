// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/concurrent/seqlock.h>

namespace concurrent {
namespace internal {

template <typename Osal>
typename SeqLock<Osal>::ReadTransactionToken SeqLock<Osal>::BeginReadTransaction() {
  SequenceNumber seq_num;

  while (((seq_num = seq_num_.load(std::memory_order_acquire)) & 0x1) != 0) {
    Osal::ArchYield();
  }

  return ReadTransactionToken{seq_num};
}

template <typename Osal>
bool SeqLock<Osal>::TryBeginReadTransaction(ReadTransactionToken& out_token,
                                            zx_duration_t timeout) {
  return TryBeginReadTransactionDeadline(out_token, Osal::GetClockMonotonic() + timeout);
}

template <typename Osal>
bool SeqLock<Osal>::TryBeginReadTransactionDeadline(ReadTransactionToken& out_token,
                                                    zx_time_t deadline) {
  while (((out_token.seq_num_ = seq_num_.load(std::memory_order_acquire)) & 0x1) != 0) {
    if (Osal::GetClockMonotonic() >= deadline) {
      return false;
    }
    Osal::ArchYield();
  }

  return true;
}

template <typename Osal>
bool SeqLock<Osal>::EndReadTransaction(ReadTransactionToken token) {
  if ((token.seq_num_ & 0x1) != 0) {
    return false;
  }

  // TODO(johngro): figure out how best to document/communicate this.  The
  // assumption is that We can get away with a relaxed load here because the
  // user just observed some data with either atomic loads with
  // memory_order_acquire, or they observed some data with relaxed atomic
  // loads followed by an atomic_thread_fence with memory_order_acquire.
  return seq_num_.load(std::memory_order_relaxed) == token.seq_num_;
}

template <typename Osal>
void SeqLock<Osal>::Acquire() {
  while (true) {
    SequenceNumber expected = seq_num_.load(std::memory_order_acquire);
    if ((expected & 0x1) == 0) {
      if (seq_num_.compare_exchange_strong(expected, expected + 1,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
        break;
      }
    }

    Osal::ArchYield();
  }
}

template <typename Osal>
bool SeqLock<Osal>::TryAcquire(zx_duration_t timeout) {
  return TryAcquireDeadline(Osal::GetClockMonotonic() + timeout);
}

template <typename Osal>
bool SeqLock<Osal>::TryAcquireDeadline(zx_time_t deadline) {
  while (true) {
    SequenceNumber expected = seq_num_.load(std::memory_order_acquire);
    if ((expected & 0x1) == 0) {
      if (seq_num_.compare_exchange_strong(expected, expected + 1,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
        return true;
      }
    }

    if (Osal::GetClockMonotonic() >= deadline) {
      return false;
    }

    Osal::ArchYield();
  }
}

template <typename Osal>
void SeqLock<Osal>::Release() {
  [[maybe_unused]] SequenceNumber before = seq_num_.fetch_add(1, std::memory_order_acq_rel);
  ZX_DEBUG_ASSERT((before & 0x1) != 0);
}

}  // namespace internal
}  // namespace concurrent
