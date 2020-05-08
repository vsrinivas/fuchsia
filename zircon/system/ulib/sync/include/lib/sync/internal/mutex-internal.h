// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYNC_INTERNAL_MUTEX_INTERNAL_H_
#define LIB_SYNC_INTERNAL_MUTEX_INTERNAL_H_

#include <assert.h>
#include <lib/sync/mutex.h>
#include <zircon/process.h>
#include <zircon/types.h>

// The value of LIB_SYNC_MUTEX_UNLOCKED must be 0 to match C11's mtx.h and so
// that mutexes can be allocated in BSS segments (zero-initialized data).
//
// Note that we use bit zero as the storage for our contested vs. unconstested
// state, but the sense of the bit is negative instead of positive.  IOW - a
// contested mutex's state is encoded as the handle of the owning thread with
// the LSB cleared (not set).
#define LIB_SYNC_MUTEX_UNLOCKED ((zx_futex_storage_t)0)
#define CONTESTED_BIT ((zx_futex_storage_t)1)

static_assert(sizeof(zx_handle_t) <= sizeof(zx_futex_storage_t),
              "mutex implementation requires futex storage to be "
              "large enough to hold a zircon handle");

static_assert((CONTESTED_BIT & ZX_HANDLE_FIXED_BITS_MASK) == CONTESTED_BIT,
              "mutex implementation requires that it's contested state storage "
              "bit be one of the zx_handle_t's guaranteed-to-be-one bits.");

static_assert((~CONTESTED_BIT & (zx_futex_storage_t)ZX_HANDLE_FIXED_BITS_MASK) != 0,
              "mutex implementation requires at least two guaranteed-to-be-one "
              "bits in zx_handle_t's");

static inline zx_futex_storage_t libsync_mutex_locked_and_uncontested(void) {
  return ((zx_futex_storage_t)_zx_thread_self());
}

static inline bool libsync_mutex_is_contested(zx_futex_storage_t val) {
  return ((val & CONTESTED_BIT) == 0);
}

static inline zx_futex_storage_t libsync_mutex_make_contested(zx_futex_storage_t val) {
  return (val & ~CONTESTED_BIT);
}

static inline zx_handle_t libsync_mutex_make_owner_from_state(zx_futex_storage_t val) {
  return (val != LIB_SYNC_MUTEX_UNLOCKED) ? (zx_handle_t)(val | CONTESTED_BIT) : ZX_HANDLE_INVALID;
}

#undef CONTESTED_BIT

#endif  // LIB_SYNC_INTERNAL_MUTEX_INTERNAL_H_
