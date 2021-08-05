// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/crashlog.h>
#include <lib/debuglog.h>
#include <lib/fit/defer.h>
#include <lib/io.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/persistent-debuglog.h>
#include <lib/version.h>
#include <platform.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <dev/udisplay.h>
#include <kernel/auto_lock.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <ktl/atomic.h>
#include <lk/init.h>
#include <vm/vm.h>

#include "debuglog_internal.h"

static_assert((DLOG_SIZE & DLOG_MASK) == 0u, "must be power of two");
static_assert(DLOG_MAX_RECORD <= DLOG_SIZE, "wat");
static_assert((DLOG_MAX_RECORD & 3) == 0, "E_DONT_DO_THAT");

static lazy_init::LazyInit<DLog, lazy_init::CheckType::None, lazy_init::Destructor::Disabled> DLOG;

FILE gDlogSerialFile{[](void*, ktl::string_view str) {
                       dlog_serial_write(str);
                       return static_cast<int>(str.size());
                     },
                     nullptr};

// dlog_bypass_ will cause printfs to directly write to console. It also has the
// side effect of disabling uart Tx interrupts, which causes all of the serial
// writes to be polling.
//
// We need to preserve the compile time switch (ENABLE_KERNEL_LL_DEBUG), even
// though we add a kernel cmdline (kernel.bypass-debuglog), to bypass the debuglog.
// This is to allow very early prints in the kernel to go to the serial console.
bool dlog_bypass_ =
#if ENABLE_KERNEL_LL_DEBUG
    true;
#else
    false;
#endif

#define DLOG_HDR_SET(fifosize, readsize) ((((readsize)&0xFFF) << 12) | ((fifosize)&0xFFF))
#define DLOG_HDR_GET_FIFOLEN(n) ((n)&0xFFF)
#define DLOG_HDR_GET_READLEN(n) (((n) >> 12) & 0xFFF)

// The debug log maintains a circular buffer of debug log records,
// consisting of a common header (dlog_header_t) followed by up
// to 224 bytes of textual log message.  Records are aligned on
// uint32_t boundaries, so the header word which indicates the
// true size of the record and the space it takes in the fifo
// can always be read with a single uint32_t* read (the header
// or body may wrap but the initial header word never does).
//
// The ring buffer position is maintained by continuously incrementing
// head and tail pointers (type size_t, so uint64_t on 64bit systems),
//
// This allows readers to trivial compute if their local tail
// pointer has "fallen out" of the fifo (an entire fifo's worth
// of messages were written since they last tried to read) and then
// they can snap their tail to the global tail and restart
//
//
// Tail indicates the oldest message in the debug log to read
// from, Head indicates the next space in the debug log to write
// a new message to.  They are clipped to the actual buffer by
// DLOG_MASK.
//
//       T                     T
//  [....XXXX....]  [XX........XX]
//           H         H

void DLog::StartThreads() {
  DEBUG_ASSERT(notifier_state_.thread == nullptr);
  DEBUG_ASSERT(dumper_state_.thread == nullptr);

  auto notifier_thunk = [](void* arg) -> int { return static_cast<DLog*>(arg)->NotifierThread(); };
  if ((notifier_state_.thread = Thread::Create(kDlogNotifierThreadName, notifier_thunk, this,
                                               HIGH_PRIORITY - 1)) != NULL) {
    notifier_state_.thread->Resume();
  }

  if (platform_serial_enabled() || platform_early_console_enabled()) {
    auto dumper_thunk = [](void* arg) -> int { return static_cast<DLog*>(arg)->DumperThread(); };
    if ((dumper_state_.thread = Thread::Create(kDlogDumperThreadName, dumper_thunk, this,
                                               HIGH_PRIORITY - 2)) != NULL) {
      dumper_state_.thread->Resume();
    }
  }
}

zx_status_t DLog::Shutdown(zx_time_t deadline) {
  dprintf(INFO, "Shutting down debuglog\n");

  // It is critical to set the shutdown flag first, to prevent new records from
  // being inserted because the dumper thread will continue to read records
  // and drain the queue even after shutdown is requested.  If we don't stop the
  // flow up stream, then a sufficiently speedy write could prevent the
  // dumper thread from terminating.
  {
    Guard<SpinLock, IrqSave> guard{&lock_};
    this->shutdown_requested_ = true;
  }

  auto ShutdownThread = [deadline](ThreadState& state, const char* name) -> zx_status_t {
    const bool already_shutdown = state.shutdown_requested.exchange(true);
    if (already_shutdown) {
      // If shutdown has already been requested then either a full debuglog shutdown has already
      // happened, or we are currently racing with one. In the former case we could immediately
      // return, but in the latter we need to wait until they have finished shutdown. Given how
      // unlikely this whole scenario is, and the comparative difficulty of synchronizing the second
      // scenario we just wait till the deadline. Most likely whoever was already shutting down the
      // debuglog will have performed halt/reboot before this sleep completes.
      Thread::Current::Sleep(deadline);
      return ZX_OK;
    }

    state.event.Signal();
    if (state.thread != nullptr) {
      zx_status_t status = state.thread->Join(nullptr, deadline);
      if (status != ZX_OK) {
        dprintf(INFO, "Failed to join %s thread: %d\n", name, status);
        return status;
      }
      state.thread = nullptr;
    }
    return ZX_OK;
  };

  // Shutdown the notifier thread first. Ordering is important because the
  // notifier thread is responsible for passing log records to the dumper.
  zx_status_t notifier_status = ShutdownThread(notifier_state_, kDlogNotifierThreadName);
  zx_status_t dumper_status = ShutdownThread(dumper_state_, kDlogDumperThreadName);

  // If one of them failed, return the status corresponding to the first
  // failure.
  if (notifier_status != ZX_OK) {
    return notifier_status;
  }
  return dumper_status;
}

void DLog::BluescreenInit() {
  // if we're panicing, stop processing log writes
  // they'll fail over to kernel console and serial
  panic_ = true;

  udisplay_bind_gfxconsole();

  // replay debug log?

  // Print panic string.
  //
  // WARNING: This string is detected by external tools to detect
  // kernel panics during tests. Changes should be made with care.
  printf("\nZIRCON KERNEL PANIC\n\n");

  // Print uptime, current CPU, and version information.
  printf("UPTIME: %" PRIi64 "ms, CPU: %" PRIu32 "\n", current_time() / ZX_MSEC(1),
         arch_curr_cpu_num());
  print_backtrace_version_info();
  g_crashlog.base_address = (uintptr_t)__code_start;
}

zx_status_t DLog::write(uint32_t severity, uint32_t flags, ktl::string_view str) {
  str = str.substr(0, DLOG_MAX_DATA);

  const char* ptr = str.data();

  const size_t len = str.size();

  if (panic_) {
    return ZX_ERR_BAD_STATE;
  }

  // Our size "on the wire" must be a multiple of 4, so we know that worst case
  // there will be room for a header preamble skipping the last n bytes when the
  // fifo wraps
  size_t wiresize = sizeof(dlog_header) + ALIGN4(len);

  // Prepare the record header before taking the lock
  dlog_header_t hdr;
  hdr.preamble = static_cast<uint32_t>(DLOG_HDR_SET(wiresize, sizeof(dlog_header) + len));
  hdr.datalen = static_cast<uint16_t>(len);
  hdr.severity = static_cast<uint8_t>(severity);
  hdr.flags = static_cast<uint8_t>(flags);
  hdr.timestamp = current_time();
  Thread* t = Thread::Current::Get();
  if (t) {
    hdr.pid = t->pid();
    hdr.tid = t->tid();
  } else {
    hdr.pid = 0;
    hdr.tid = 0;
  }

  bool holding_thread_lock;
  {
    Guard<SpinLock, IrqSave> guard{&lock_};

    hdr.sequence = sequence_count_;

    if (shutdown_requested_) {
      return ZX_ERR_BAD_STATE;
    }

    // Discard records at tail until there is enough
    // space for the new record.
    while ((head_ - tail_) > (DLOG_SIZE - wiresize)) {
      uint32_t preamble = *reinterpret_cast<uint32_t*>(data_ + (tail_ & DLOG_MASK));
      tail_ += DLOG_HDR_GET_FIFOLEN(preamble);
    }

    size_t offset = head_ & DLOG_MASK;
    size_t fifospace = DLOG_SIZE - offset;

    if (fifospace >= wiresize) {
      // everything fits in one write, simple case!
      memcpy(data_ + offset, &hdr, sizeof(hdr));
      memcpy(data_ + offset + sizeof(hdr), ptr, len);
    } else if (fifospace < sizeof(hdr)) {
      // the wrap happens in the header
      memcpy(data_ + offset, &hdr, fifospace);
      memcpy(data_, reinterpret_cast<uint8_t*>(&hdr) + fifospace, sizeof(hdr) - fifospace);
      memcpy(data_ + (sizeof(hdr) - fifospace), ptr, len);
    } else {
      // the wrap happens in the data
      memcpy(data_ + offset, &hdr, sizeof(hdr));
      offset += sizeof(hdr);
      fifospace -= sizeof(hdr);
      memcpy(data_ + offset, ptr, fifospace);
      memcpy(data_, ptr + fifospace, len - fifospace);
    }
    head_ += wiresize;
    sequence_count_++;

    // Need to check this before re-releasing the log lock, since we may
    // re-enable interrupts while doing that.  If interrupts are enabled when we
    // make this check, we could see the following sequence of events between
    // two CPUs and incorrectly conclude we are holding the thread lock:
    // C2: Acquire thread_lock
    // C1: Running this thread, evaluate thread_lock.HolderCpu() -> C2
    // C1: Context switch away
    // C2: Release thread_lock
    // C2: Context switch to this thread
    // C2: Running this thread, evaluate arch_curr_cpu_num() -> C2
    holding_thread_lock = thread_lock.HolderCpu() == arch_curr_cpu_num();
  }

  // If we happen to be called from within the global thread lock, use a special
  // version of event signal.
  if (holding_thread_lock) {
    thread_lock.AssertHeld();
    notifier_state_.event.SignalLocked();
  } else {
    notifier_state_.event.Signal();
  }

  return ZX_OK;
}

// The debuglog notifier thread observes when the debuglog is
// written and calls the notify callback on any readers that
// have one so they can process new log messages.
int DLog::NotifierThread() {
  for (;;) {
    if (notifier_state_.shutdown_requested.load()) {
      break;
    }
    notifier_state_.event.Wait();

    // notify readers that new DLOG items were posted
    {
      Guard<Mutex> guard(&readers_lock_);
      for (DlogReader& reader : readers) {
        reader.Notify();
      }
    }
  }
  return ZX_OK;
}

int DLog::DumperThread() {
  // assembly buffer with room for log text plus header text
  char tmp[DLOG_MAX_DATA + 128];

  dlog_record_t rec;
  DlogReader reader;
  reader.Initialize([](void* cookie) { static_cast<Event*>(cookie)->Signal(); },
                    &dumper_state_.event);

  auto disconnect = fit::defer([&reader]() { reader.Disconnect(); });

  uint64_t expected_sequence = 0;

  bool done = false;
  while (!done) {
    dumper_state_.event.Wait();

    // If shutdown has been requested, this will be our last loop iteration.
    //
    // We do not break early because we guarantee that any messages logged prior to the start of the
    // shutdown sequence will be emitted.
    done = dumper_state_.shutdown_requested.load();

    // Read out all the records and dump them to the kernel console.
    size_t actual;
    while (reader.Read(0, &rec, &actual) == ZX_OK) {
      uint64_t gap = rec.hdr.sequence - expected_sequence;
      if (gap > 0) {
        int n = snprintf(tmp, sizeof(tmp), "debuglog: dropped %zu messages\n", gap);
        if (n > static_cast<int>(sizeof(tmp))) {
          n = sizeof(tmp);
        }
        ktl::string_view str{tmp, static_cast<size_t>(n)};
        console_write(str);
        dlog_serial_write(str);
      }
      expected_sequence = rec.hdr.sequence + 1;

      // "Remove" any tailing newline character before formatting because the
      // format string already contains a newline.
      if (rec.hdr.datalen > 0 && (rec.data[rec.hdr.datalen - 1] == '\n')) {
        rec.hdr.datalen--;
      }
      int n = snprintf(tmp, sizeof(tmp), "[%05d.%03d] %05" PRIu64 ":%05" PRIu64 "> %.*s\n",
                       (int)(rec.hdr.timestamp / ZX_SEC(1)),
                       (int)((rec.hdr.timestamp / ZX_MSEC(1)) % 1000ULL), rec.hdr.pid, rec.hdr.tid,
                       rec.hdr.datalen, rec.data);
      if (n > (int)sizeof(tmp)) {
        n = sizeof(tmp);
      }
      ktl::string_view str{tmp, static_cast<size_t>(n)};
      console_write(str);
      dlog_serial_write(str);
    }
  }

  return 0;
}

// TODO: support reading multiple messages at a time
// TODO: filter with flags
zx_status_t DlogReader::Read(uint32_t flags, dlog_record_t* record, size_t* _actual) {
  DLog* log = log_;
  zx_status_t status = ZX_ERR_SHOULD_WAIT;

  {
    Guard<SpinLock, IrqSave> guard{&log->lock_};

    size_t rtail = tail_;

    // If the read-tail is not within the range of log-tail..log-head
    // this reader has been lapped by a writer and we reset our read-tail
    // to the current log-tail.
    //
    if ((log->head_ - log->tail_) < (log->head_ - rtail)) {
      rtail = log->tail_;
    }

    if (rtail != log->head_) {
      size_t offset = (rtail & DLOG_MASK);
      void* record_start = log->data_ + offset;
      uint32_t header = *reinterpret_cast<uint32_t*>(record_start);

      size_t actual = DLOG_HDR_GET_READLEN(header);
      size_t fifospace = DLOG_SIZE - offset;

      if (fifospace >= actual) {
        // The record is contiguous.
        memcpy(record, record_start, actual);
      } else {
        // The record wraps.
        memcpy(record, record_start, fifospace);
        memcpy(reinterpret_cast<char*>(record) + fifospace, log->data_, actual - fifospace);
      }
      record->hdr.preamble = 0;

      *_actual = actual;
      status = ZX_OK;

      rtail += DLOG_HDR_GET_FIFOLEN(header);
    }

    tail_ = rtail;
  }

  return status;
}

DlogReader::~DlogReader() {
  // DlogReaders must be disconnected when destroyed.
  DEBUG_ASSERT(!InContainer());
}

void DlogReader::Initialize(NotifyCallback* notify, void* cookie) {
  // A DlogReader can only be initialized once.
  DEBUG_ASSERT(log_ == nullptr);

  log_ = &DLOG.Get();
  notify_ = notify;
  cookie_ = cookie;

  Guard<Mutex> readers_guard(&log_->readers_lock_);
  log_->readers.push_back(this);

  bool do_notify = false;

  {
    Guard<SpinLock, IrqSave> guard{&log_->lock_};
    tail_ = log_->tail_;
    do_notify = (log_->tail_ != log_->head_);
  }

  // simulate notify callback for events that arrived
  // before we were initialized
  if (do_notify && notify) {
    notify(cookie);
  }
}

void DlogReader::InitializeForTest(DLog* log) {
  // A DlogReader can only be initialized once.
  DEBUG_ASSERT(log_ == nullptr);

  log_ = log;

  Guard<Mutex> readers_guard(&log->readers_lock_);
  log->readers.push_back(this);

  {
    Guard<SpinLock, IrqSave> guard{&log->lock_};
    tail_ = log->tail_;
  }
}

void DlogReader::Disconnect() {
  if (log_) {
    Guard<Mutex> guard(&log_->readers_lock_);
    log_->readers.erase(*this);
  }
}

void DlogReader::Notify() {
  if (notify_) {
    notify_(cookie_);
  }
}

// Called first thing in init, so very early printfs can go to serial console.
void dlog_init_early() {
  // Construct the debuglog. Done here so we can construct it manually before
  // the global constructors are run.
  DLOG.Initialize();
  persistent_dlog_init_early();
}

// Called after kernel cmdline options are parsed (in platform_early_init()).
// The compile switch (if enabled) overrides the kernel cmdline switch.
void dlog_bypass_init() {
  if (dlog_bypass_ == false) {
    dlog_bypass_ = gBootOptions->bypass_debuglog;
  }
}

zx_status_t dlog_write(uint32_t severity, uint32_t flags, ktl::string_view str) {
  return DLOG->write(severity, flags, str);
}

// Common bottleneck between sys_debug_write() and debuglog_dumper()
// to reduce interleaved messages between the serial console and the
// debuglog drainer.

namespace {
DECLARE_SINGLETON_MUTEX(DlogSerialWriteLock);
}  // namespace

void dlog_serial_write(ktl::string_view str) {
  if (dlog_bypass_ == true) {
    // If LL DEBUG is enabled we take this path which uses a spinlock
    // and prevents the direct writes from the kernel from interleaving
    // with our output
    serial_write(str);
  } else {
    // Otherwise we can use a mutex and avoid time under spinlock
    Guard<Mutex> guard{DlogSerialWriteLock::Get()};
    platform_dputs_thread(str.data(), str.size());
  }
}

void dlog_bluescreen_init() { DLOG->BluescreenInit(); }
void dlog_force_panic() { dlog_bypass_ = true; }
zx_status_t dlog_shutdown(zx_time_t deadline) { return DLOG->Shutdown(deadline); }

LK_INIT_HOOK(
    debuglog, [](uint level) { DLOG->StartThreads(); }, LK_INIT_LEVEL_PLATFORM)
