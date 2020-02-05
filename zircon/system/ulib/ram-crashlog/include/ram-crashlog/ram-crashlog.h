// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RAM_CRASHLOG_RAM_CRASHLOG_H_
#define RAM_CRASHLOG_RAM_CRASHLOG_H_

#include <zircon/boot/crash-reason.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Overview:
//
// A "RAM" crashlog is a data structure meant to hold details about a
// kernel-level crash across reboots.  It is meant to exist in contiguously
// mapped RAM storage.  Ideally, it would use on-die static RAM, but it can also
// use dynamic RAM if there are no other options.  In the case of DRAM, systems
// need to take care to enable RAM refresh as early as possible in order to
// avoid degradation and corruption of the crashlog during a reboot.
//
// Logically, a crashlog consists of a header and a user supplied payload.  The
// header contains a small amount of common information meant to be updated
// periodically in the case of a spontaneous reboot (such as those caused by
// hardware watchdog timers, brownout detectors, or reboots triggered by higher
// execution levels).  In the case that a reboot is triggered by the system
// itself (out-of-memory condition, kernel panic, etc.) the crashlog can also
// hold some amount of additional user supplied data.
//
// In RAM, the crashlog actually holds two copies of the header in order to
// allow for a transactional update of the log.  During updates of the header,
// the new header is written and flushed first, and then the magic number is
// flipped in order to indicate a change in the active header.  In addition,
// separate CRC32s are stored for the header and payload sections of the
// crashlog.  This allows the system to detect and be aware of corruption in the
// payload section of the crashlog, but still deliver some data (along with the
// knowledge that some of the bits in the payload may have been flipped)
//
// CRC integrity checks use CRC32 (ISO 3309)
//

#define RAM_CRASHLOG_MAGIC_0 ((uint64_t)(0x6f8962d66b28504f))
#define RAM_CRASHLOG_MAGIC_1 (~RAM_CRASHLOG_MAGIC_0)

typedef struct {
  zx_duration_t uptime;          // Best estimate of system uptime.
  zircon_crash_reason_t reason;  // The system's best guess as to the reason for crash/reboot
  uint32_t payload_len;
  uint32_t payload_crc32;  // A CRC32 of just the payload section of the crashlog.
  uint32_t header_crc32;   // A CRC32 of just this header, excluding |header_crc32|.
} ram_crashlog_header_t;

typedef struct {
  // Magic number sanity check.  Also indicates which of the following two
  // headers is considered active.  A value of MAGIC_0 indicates that hdr[0] is
  // active, while MAGIC_1 indicates that hdr[1] is active.
  uint64_t magic;

  // The two copies of the headers.  Check |magic| to see which (if any) is valid.
  ram_crashlog_header_t hdr[2];

  // Payload comes immediately after the top level header structure.
} ram_crashlog_t;

// A structure used to provide details about a recovered crashlog.  When a
// crashlog is recovered successfully, this structure will convey:
// 1) The crash reason
// 2) The uptime estimate
// 3) An indication of the payload's integrity.
// 4) A pointer/length to the memory mapped payload.
typedef struct {
  zx_duration_t uptime;
  zircon_crash_reason_t reason;
  bool payload_valid;
  const void* payload;
  uint32_t payload_len;
} recovered_ram_crashlog_t;

// Stash as much crashlog as we can at the location pointed to by buf.  The
// number of bytes stored will never exceed |buf_len|.  This operation will
// overwrite any existing crashlog located at |buf|.
//
// A successfully stashed crashlog will hold the user supplied uptime and
// software reboot reason, in addition to any extra user supplied log.  This
// allows users to periodically stash an uptime and "unknown" reboot reason in
// the event that the system is spontaneously rebooted by hardware (typically
// because of a watchdog timer), in addition to recording a more complete
// crashlog in the case of a software induced crash (typically due to either
// kernel panic, or a system-wide OOM condition)
//
// Params:
// |buf|         : A pointer to the location to actually stow the log
// |buf_len|     : The max number of bytes which may be stored at |buf|
// |payload|     : The optional pointer to the payload to be stored.  May be NULL.
// |payload_len| : The length of the payload buffer.  Must be 0 if |payload| is NULL
// |sw_resaon|   : The reason that the system crashed, according to software.
// |uptime|      : Our best guess as to the uptime of the system before reboot/crash.
//
// Return Values:
// ZX_INVALID_ARGS     : Something is wrong with either buf, payload, payload_len, or sw_reason
// ZX_BUFFER_TOO_SMALL : buf_len is too small to hold a crashlog, even if the payload is 0 bytes
//                       long.
// ZX_OK               : Log was successfully stowed.
//
zx_status_t ram_crashlog_stow(void* buf, size_t buf_len, const void* payload, uint32_t payload_len,
                              zircon_crash_reason_t sw_reason, zx_time_t uptime);

// Attempt to recover the crashlog located at |buf|, returning details about the
// recovered log in the user-supplied log_out structure.  Provided that valid
// arguments are supplied, crashlog recovery will only ever completely fail
// because of either a bad internal magic number or a corrupt header.  Payloads
// with invalid lengths or invalid CRC will not result in the call to recover
// failing.  Instead as much of the payload as possible will be supplied to the
// user, and the "payload_valid" flag in the |recovered_ram_crashlog_t| will be
// set to false to indicate that the bits cannot be completely trusted.
//
// Params:
// |buf|     : A pointer to the location to recover the log from.
// |buf_len| : The max number of bytes which may be read from |buf|
// |log_out| : The user supplied structure which will hold the recovered log
//             details.
//
// Return Values:
// ZX_INVALID_ARGS      : Something is wrong with either buf, log_out.
// ZX_BUFFER_TOO_SMALL  : buf_len is too small to hold a crashlog, even if the payload is 0 bytes
//                        long.
// ZX_IO_DATA_INTEGRITY : The log failed fundamental sanity checks and cannot be recovered.
// ZX_OK                : Log was successfully recovered.
//
zx_status_t ram_crashlog_recover(const void* buf, size_t buf_len,
                                 recovered_ram_crashlog_t* log_out);

__END_CDECLS

#endif  // RAM_CRASHLOG_RAM_CRASHLOG_H_
