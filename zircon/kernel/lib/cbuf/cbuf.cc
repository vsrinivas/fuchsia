// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <lib/cbuf.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <ktl/bit.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

// This should only be called once to initialize the Cbuf, and so thread safety analysis is
// disabled.
void Cbuf::Initialize(size_t len, void* buf) TA_NO_THREAD_SAFETY_ANALYSIS {
  DEBUG_ASSERT(len > 0);
  DEBUG_ASSERT(ktl::has_single_bit(len));

  len_pow2_ = static_cast<uint32_t>(log2_ulong_floor(len));
  buf_ = static_cast<char*>(buf);

  LTRACEF("len %zu, len_pow2 %u\n", len, len_pow2_);
}

// TODO(fxbug.dev/48878): We want to revisit the Cbuf API. It's intended to be used from interrupt
// context, at which time clients can rely on being the only accessor. For now, we disable thread
// safety analysis on this function.
bool Cbuf::Full() const TA_NO_THREAD_SAFETY_ANALYSIS {
  uint32_t consumed = modpow2(head_ - tail_, len_pow2_);
  size_t avail = valpow2(len_pow2_) - consumed - 1;
  return avail == 0;
}

size_t Cbuf::WriteChar(char c) {
  {
    AutoSpinLock guard(&lock_);

    if (Full()) {
      return 0;
    }

    buf_[head_] = c;
    IncPointer(&head_, 1);
  }

  // By signaling after dropping the lock, we avoid lock thrashing (though it doesn't matter much
  // since this lock is a spinlock).
  //
  // Note: by the time we signal, the buffer may have already been drained, but that's OK.  It just
  // means a reader may be woken when the buffer is empty.
  event_.Signal();

  return 1;
}

zx::result<char> Cbuf::ReadChar(bool block) {
  while (true) {
    {
      AutoSpinLock guard(&lock_);

      if (!Empty()) {
        char c = buf_[tail_];
        IncPointer(&tail_, 1);
        if (Empty()) {
          event_.Unsignal();
        }
        return zx::ok(c);
      }

      // Because the signal state does not 100% match the buffer state, it is critical that the
      // event is unsignaled when the buffer is found to be empty (not just when it *transitions* to
      // empty).
      event_.Unsignal();
    }

    if (!block) {
      return zx::error(ZX_ERR_SHOULD_WAIT);
    }

    zx_status_t status = event_.Wait(Deadline::infinite());
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }
}

bool Cbuf::Empty() const { return (tail_ == head_); }
