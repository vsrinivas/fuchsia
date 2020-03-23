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

#include <fbl/algorithm.h>
#include <kernel/auto_lock.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>

#define LOCAL_TRACE 0

// This should only be called once to initialize the Cbuf, and so thread safety analysis is
// disabled.
void Cbuf::Initialize(size_t len, void* buf) TA_NO_THREAD_SAFETY_ANALYSIS {
  DEBUG_ASSERT(len > 0);
  DEBUG_ASSERT(fbl::is_pow2(len));

  len_pow2_ = log2_ulong_floor(len);
  buf_ = static_cast<char*>(buf);

  LTRACEF("len %zu, len_pow2 %u\n", len, len_pow2_);
}

// TODO(fxb/48878) We want to revisit the Cbuf API. It's intended to be used from interrupt context,
// at which time clients can rely on being the only accessor. For now, we disable thread safety
// analysis on this function.
size_t Cbuf::SpaceAvail() const TA_NO_THREAD_SAFETY_ANALYSIS {
  uint32_t consumed = modpow2(head_ - tail_, len_pow2_);
  return valpow2(len_pow2_) - consumed - 1;
}

size_t Cbuf::WriteChar(char c) {
  size_t ret = 0;
  {
    AutoSpinLock guard(&lock_);

    if (SpaceAvail() > 0) {
      buf_[head_] = c;
      IncPointer(&head_, 1);
      ret = 1;
    }
  }

  if (ret > 0) {
    event_.Signal();
  }

  return ret;
}

size_t Cbuf::ReadChar(char* c, bool block) {
  DEBUG_ASSERT(c);

retry:
  if (block) {
    event_.Wait(Deadline::infinite());
  }

  size_t ret = 0;
  {
    AutoSpinLock guard(&lock_);

    // see if there's data available
    if (tail_ != head_) {
      *c = buf_[tail_];
      IncPointer(&tail_, 1);

      if (tail_ == head_) {
        // We've emptied the buffer, so unsignal the event.
        event_.Unsignal();
      }

      ret = 1;
    }
  }

  if (block && ret == 0) {
    goto retry;
  }

  return ret;
}
