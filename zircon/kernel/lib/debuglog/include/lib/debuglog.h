// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEBUGLOG_INCLUDE_LIB_DEBUGLOG_H_
#define ZIRCON_KERNEL_LIB_DEBUGLOG_INCLUDE_LIB_DEBUGLOG_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <kernel/event.h>
#include <kernel/mutex.h>

struct DLog;
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

  zx_status_t Read(uint32_t flags, void* ptr, size_t len, size_t* actual);

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

#define DLOG_HDR_SET(fifosize, readsize) ((((readsize)&0xFFF) << 12) | ((fifosize)&0xFFF))

#define DLOG_HDR_GET_FIFOLEN(n) ((n)&0xFFF)
#define DLOG_HDR_GET_READLEN(n) (((n) >> 12) & 0xFFF)

#define DLOG_MIN_RECORD (32u)
#define DLOG_MAX_DATA (224u)
#define DLOG_MAX_RECORD (DLOG_MIN_RECORD + DLOG_MAX_DATA)

// This structure is designed to be copied into a zx_log_record_t from zircon/syscalls/log.h . Only
// the header field is repurposed, and the rest is then transferred to userspace as-is.
struct dlog_header {
  uint32_t header;
  uint16_t datalen;
  uint8_t severity;
  uint8_t flags;
  uint64_t timestamp;
  uint64_t pid;
  uint64_t tid;
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

static_assert(sizeof(dlog_header_t) == DLOG_MIN_RECORD, "");
static_assert(sizeof(dlog_record_t) == DLOG_MAX_RECORD, "");

zx_status_t dlog_write(uint32_t severity, uint32_t flags, const void* ptr, size_t len);

// used by sys_debug_write()
void dlog_serial_write(const char* data, size_t len);

// bluescreen_init should be called at the "start" of a fatal fault or
// panic to ensure that the fault output (via kernel printf/dprintf)
// is captured or displayed to the user
void dlog_bluescreen_init(void);

// Force the dlog into panic mode.  Can be used in special circumstances to
// force log messages to the serial console in the event that interrupts are off
// and will never be turned back on (for example, when about to force a watchdog
// to fire).
void dlog_force_panic(void);

// Shutdown the debuglog subsystem.
//
// Blocks, waiting up to |deadline|, for dlog threads to terminate.
//
// On failure, the debuglog subsystem is left in an undefined state.
//
// Returns ZX_OK on success.
zx_status_t dlog_shutdown(zx_time_t deadline);

void dlog_bypass_init_early(void);
void dlog_bypass_init(void);
bool dlog_bypass(void);

// A printf-like macro which prepend's the user's message with the special
// "ZIRCON KERNEL OOPS" tag.  zbi_test bots look for tags like this and consider
// their presence to indicate test failures, even if the higher level test
// framework code thinks the test passed.
#define DLOG_KERNEL_OOPS(fmt, ...) printf("\nZIRCON KERNEL OOPS\n" fmt, ##__VA_ARGS__)

#endif  // ZIRCON_KERNEL_LIB_DEBUGLOG_INCLUDE_LIB_DEBUGLOG_H_
