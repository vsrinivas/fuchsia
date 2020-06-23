// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEBUGLOG_DEBUGLOG_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_DEBUGLOG_DEBUGLOG_INTERNAL_H_

#include <lib/zircon-internal/thread_annotations.h>

#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <ktl/string_view.h>

#define DLOG_SIZE (128u * 1024u)
#define DLOG_MASK (DLOG_SIZE - 1u)

#define ALIGN4_TRUNC(n) ((n) & (~3))
#define ALIGN4(n) ALIGN4_TRUNC(((n) + 3))

struct DLog {
  explicit constexpr DLog() {}

  zx_status_t write(uint32_t severity, uint32_t flags, ktl::string_view str);

  // Mark this DLog as being shutdown.  Once called, subsequent |write| operations will fail.
  void shutdown();

  SpinLock lock;

  size_t head TA_GUARDED(lock) = 0;
  size_t tail TA_GUARDED(lock) = 0;

  // Indicates that the system has begun to panic.  When true, |write| will immediately return an
  // error.
  bool panic = false;

  AutounsignalEvent event;

  DECLARE_LOCK(DLog, Mutex) readers_lock;
  fbl::DoublyLinkedList<DlogReader*> readers;

  uint8_t data[DLOG_SIZE]{0};

 private:
  // Indicates that this |DLog| object is being shutdown.  When true, |write| will immediately
  // return an error.
  bool shutdown_requested_ TA_GUARDED(lock) = false;
};

#endif  // ZIRCON_KERNEL_LIB_DEBUGLOG_DEBUGLOG_INTERNAL_H_
