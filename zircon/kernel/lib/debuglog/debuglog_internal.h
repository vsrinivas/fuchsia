// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEBUGLOG_DEBUGLOG_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_DEBUGLOG_DEBUGLOG_INTERNAL_H_

#include <lib/debuglog.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <ktl/string_view.h>

#define DLOG_SIZE (128u * 1024u)
#define DLOG_MASK (DLOG_SIZE - 1u)

#define ALIGN4_TRUNC(n) ((n) & (~3))
#define ALIGN4(n) ALIGN4_TRUNC(((n) + 3))

class DlogReader;
struct DebuglogTests;

class DLog {
 public:
  explicit constexpr DLog() {}

  void StartThreads();

  // Mark this DLog as being shutdown, then shutdown all threads.  Once called,
  // subsequent |write| operations will fail.
  zx_status_t Shutdown(zx_time_t deadline);

  void BluescreenInit();

  zx_status_t write(uint32_t severity, uint32_t flags, ktl::string_view str);

 private:
  friend struct DebuglogTests;
  friend class DlogReader;

  struct ThreadState {
    Thread* thread{nullptr};
    ktl::atomic<bool> shutdown_requested{false};
    AutounsignalEvent event;
  };

  static inline constexpr char kDlogNotifierThreadName[] = "debuglog-notifier";
  static inline constexpr char kDlogDumperThreadName[] = "debuglog-dumper";

  int NotifierThread();
  int DumperThread();

  ThreadState notifier_state_;
  ThreadState dumper_state_;

  DECLARE_SPINLOCK(DLog) lock_;
  DECLARE_LOCK(DLog, Mutex) readers_lock_;

  size_t head_ TA_GUARDED(lock_) = 0;
  size_t tail_ TA_GUARDED(lock_) = 0;

  // Indicates that the system has begun to panic.  When true, |write| will
  // immediately return an error.
  bool panic_ = false;

  // The list of our current readers.
  fbl::DoublyLinkedList<DlogReader*> readers TA_GUARDED(readers_lock_);

  // A counter incremented for each log message that enters the debuglog.
  uint64_t sequence_count_ TA_GUARDED(lock_) = 0;

  // Indicates that this |DLog| object is being shutdown.  When true, |write| will immediately
  // return an error.
  bool shutdown_requested_ TA_GUARDED(lock_) = false;

  uint8_t data_[DLOG_SIZE]{0};
};

#endif  // ZIRCON_KERNEL_LIB_DEBUGLOG_DEBUGLOG_INTERNAL_H_
