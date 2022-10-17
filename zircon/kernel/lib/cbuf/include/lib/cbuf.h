// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009-2013 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CBUF_INCLUDE_LIB_CBUF_H_
#define ZIRCON_KERNEL_LIB_CBUF_INCLUDE_LIB_CBUF_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <pow2.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <kernel/event.h>
#include <kernel/spinlock.h>

class Cbuf {
 public:
  /**
   * Constructor
   *
   * Create a Cbuf structure with no underlying data buffer. A subsequent call to |Initialize| must
   * be made to allocate an underlying data buffer.
   */
  Cbuf() = default;

  /**
   * Initialize
   *
   * Initialize a cbuf structure using the supplied buffer for internal storage.
   *
   * @param[in] len The size of the supplied buffer, in bytes.
   * @param[in] buf A pointer to the memory to be used for internal storage.
   */
  void Initialize(size_t len, void* buf);

  /**
   * Full
   *
   * Whether the Cbuf has free space available.
   */
  bool Full() const;

  /**
   * Read one character.
   *
   * If |wait| is true, block until a character is read or the thread is asked to suspend or
   * terminate (ZX_ERR_INTERNAL_INTR_RETRY, ZX_ERR_INTERNAL_INTR_KILLED).
   *
   * If |wait| is false and no character is ready, ZX_ERR_SHOULD_WAIT is returned.
   */
  zx::result<char> ReadChar(bool block);

  size_t WriteChar(char c);

 private:
  // Returns true iff the buffer is empty.
  bool Empty() const TA_REQ(lock_);

  void IncPointer(uint32_t* ptr, uint inc) TA_REQ(lock_) { *ptr = modpow2(*ptr + inc, len_pow2_); }

  uint32_t head_ TA_GUARDED(lock_) = 0;
  uint32_t tail_ TA_GUARDED(lock_) = 0;
  uint32_t len_pow2_ TA_GUARDED(lock_) = 0;
  char* buf_ TA_GUARDED(lock_) = nullptr;
  Event event_;
  SpinLock lock_;
};

#endif  // ZIRCON_KERNEL_LIB_CBUF_INCLUDE_LIB_CBUF_H_
