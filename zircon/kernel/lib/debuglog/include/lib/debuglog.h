// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEBUGLOG_INCLUDE_LIB_DEBUGLOG_H_
#define ZIRCON_KERNEL_LIB_DEBUGLOG_INCLUDE_LIB_DEBUGLOG_H_

#include <stdint.h>
#include <stdio.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <zircon/syscalls/log.h>
#include <zircon/types.h>

#include <kernel/event.h>
#include <kernel/mutex.h>
#include <ktl/string_view.h>
#include <ktl/type_traits.h>
#include <ktl/span.h>

class DLog;
typedef struct dlog_header dlog_header_t;
typedef struct dlog_record dlog_record_t;

// DlogReaders drain debuglogs. Owners of DlogReaders are called back as
// messages are pushed through the debuglog, via the Notify callback.
class DlogReader : public fbl::DoublyLinkedListable<DlogReader*> {
 public:
  using NotifyCallback = void(void* cookie);

  constexpr DlogReader() {}
  ~DlogReader();

  // Since DlogReaders typically capture containing objects via |cookie_|, they
  // use 2-phase initialization to avoid races in the contruction of the
  // DlogReader and the containing object.
  void Initialize(NotifyCallback* notify, void* cookie);

  void InitializeForTest(DLog* log);

  // Read one record out of the log and store it in |record|.
  //
  // Upon success, returns ZX_OK and sets *|actual| to the record's size.
  zx_status_t Read(uint32_t flags, dlog_record_t* record, size_t* actual);

  void Notify();

  // Similar to Initialize, DlogReaders are be manually stopped via |Disconnect|
  // to avoid reentrency issues in the DlogReader and its containing object.
  //
  // Disconnect must be called before the destructor runs, if Initialize was called.
  void Disconnect();

 private:
  DLog* log_ = nullptr;
  size_t tail_ = 0;

  NotifyCallback* notify_ = nullptr;
  void* cookie_ = nullptr;
};

#define DLOG_MAX_RECORD (size_t{256})
#define DLOG_MAX_DATA (DLOG_MAX_RECORD - sizeof(dlog_header))

// This structure is designed to be copied into a zx_log_record_t from
// zircon/syscalls/log.h.
//
// The size, type, and offset of these fields must match those of
// zx_log_record_t.
struct dlog_header {
  // When inside a debuglog, the |preamble| contains both the record's true size
  // (|DLOG_HDR_READLEN|) and the record's size when padded out to live in the
  // FIFO (|DLOG_HDR_FIFOLEN|).
  //
  // After being read out of a debuglog, the |preamble| field is 0.
  uint32_t preamble;
  uint16_t datalen;
  uint8_t severity;
  uint8_t flags;
  zx_time_t timestamp;
  uint64_t pid;
  uint64_t tid;
  // Each log record is assigned a sequence number at the time it enters the
  // debuglog. A record's sequence number will be exactly one greater than the
  // record that preceeded it. The purpose of |sequence| is to enable debuglog
  // readers to detect dropped message.
  uint64_t sequence;
};

// Severity Levels
#define DEBUGLOG_TRACE (0x10)
#define DEBUGLOG_DEBUG (0x20)
#define DEBUGLOG_INFO (0x30)
#define DEBUGLOG_WARNING (0x40)
#define DEBUGLOG_ERROR (0x50)
#define DEBUGLOG_FATAL (0x60)

struct dlog_record {
  dlog_header_t hdr;
  char data[DLOG_MAX_DATA];
};

static_assert(sizeof(dlog_record_t) == DLOG_MAX_RECORD, "");

zx_status_t dlog_write(uint32_t severity, uint32_t flags, ktl::string_view str);

// used by sys_debug_write()
void dlog_serial_write(ktl::string_view str);

// Allow FILE*-based APIs, like fprintf, to use the same output
// backend as dlog_serial_write.
extern FILE gDlogSerialFile;

// bluescreen_init should be called at the "start" of a fatal fault or
// panic to ensure that the fault output (via kernel printf/dprintf)
// is captured or displayed to the user
void dlog_bluescreen_init();

// Force the dlog into panic mode.  Can be used in special circumstances to
// force log messages to the serial console in the event that interrupts are off
// and will never be turned back on (for example, when about to force a watchdog
// to fire).
void dlog_force_panic();

// Initialize the debuglog subsystem. Called once at extremely early boot.
void dlog_init_early();

// Shutdown the debuglog subsystem.
//
// Blocks, waiting up to |deadline|, for dlog threads to terminate.
//
// On failure, the debuglog subsystem is left in an undefined state.
//
// Returns ZX_OK on success.
zx_status_t dlog_shutdown(zx_time_t deadline);

// Called as soon as command line parsing is available to check for any kernel
// command line options that may affect the debug log.
void dlog_bypass_init();

// Accessor to quickly determine if the debuglog bypass is enabled.
static inline bool dlog_bypass() {
  extern bool dlog_bypass_;
  return dlog_bypass_;
}

// Renders as many of the recent debug log entries as will fit into the memory
// region specified by |target|.  Returns the number of bytes of target which
// were filled.
size_t dlog_render_to_crashlog(ktl::span<char> target);

#endif  // ZIRCON_KERNEL_LIB_DEBUGLOG_INCLUDE_LIB_DEBUGLOG_H_
