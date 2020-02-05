// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <stdint.h>
#include <string.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/errors.h>

#include <ram-crashlog/ram-crashlog.h>

namespace {
// When this module is used in the actual kernel, we need to make certain
// to actually clean the cache at very specific points in a crashlog update
// sequence.  If this is being built for user-mode, then the module is only
// being built for testing, and cache scrubbing is not needed.
#if _KERNEL
extern "C" void arch_clean_cache_range(uintptr_t start, size_t len);
void clean_cache_range(void* ptr, size_t len) {
  arch_clean_cache_range(reinterpret_cast<uintptr_t>(ptr), len);
}
#else
void clean_cache_range(void* ptr, size_t len) {}
#endif
}  // namespace

zx_status_t ram_crashlog_stow(void* buf, size_t buf_len, const void* payload, uint32_t payload_len,
                              zircon_crash_reason_t sw_reason, zx_time_t uptime) {
  // The user needs to provide a valid buffer to stow the log, but they do not
  // have to provide a payload.  That said, they may not provide a null payload
  // pointer and a non-zero payload length.
  if ((buf == nullptr) || ((payload == nullptr) && (payload_len != 0))) {
    return ZX_ERR_INVALID_ARGS;
  }

  // We cannot stow a crashlog if the buffer provided to us is too small.  It
  // has to be large enough to hold the common payload structure, at a minimum.
  if (buf_len < sizeof(ram_crashlog_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // Sanity check the reboot reason.  Invalid reasons must be rejected.
  switch (sw_reason) {
    case ZirconCrashReason::Unknown:
    case ZirconCrashReason::NoCrash:
    case ZirconCrashReason::Oom:
    case ZirconCrashReason::Panic:
    case ZirconCrashReason::SoftwareWatchdog:
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  // Figure out how much space we have for payload.  It is not an error for the
  // user to attempt to store more payload than we have room for, but we have to
  // truncate the payload in this situation.
  size_t max_payload = buf_len - sizeof(ram_crashlog_t);
  if (payload_len > max_payload) {
    payload_len = static_cast<uint32_t>(max_payload);
  }

  // Great!  Time to get to work.  Start by figuring out which instance of the
  // header we should eventually occupy.
  ram_crashlog_t& log = *(reinterpret_cast<ram_crashlog_t*>(buf));
  uint8_t* tgt_payload = reinterpret_cast<uint8_t*>(&log + 1);
  uint64_t next_magic;
  ram_crashlog_header_t* hdr;

  if (log.magic == RAM_CRASHLOG_MAGIC_0) {
    next_magic = RAM_CRASHLOG_MAGIC_1;
    hdr = &log.hdr[1];
  } else {
    next_magic = RAM_CRASHLOG_MAGIC_0;
    hdr = &log.hdr[0];
  }

  // Now fill out the header we chose to overwrite, computing the payload
  // CRC in the process.
  hdr->uptime = uptime;
  hdr->reason = sw_reason;
  hdr->payload_len = payload_len;
  hdr->payload_crc32 = crc32(0, reinterpret_cast<const uint8_t*>(payload), payload_len);

  // Compute the header CRC, then make sure it has been flushed all of the way
  // to RAM.
  hdr->header_crc32 = crc32(0, reinterpret_cast<const uint8_t*>(hdr),
                            offsetof(ram_crashlog_header_t, header_crc32));
  clean_cache_range(hdr, sizeof(*hdr));

  // Copy the payload into place (if we have any) and make sure that it has been
  // written all of the way out to physical RAM.  The old header is active at
  // this point.  If we had a non-empty payload previously to this, we are
  // almost certainly going to fail to recover the payload if we were to
  // spontaneously reboot right at this instant.  That said, we will still
  // attempt to recover whatever the old header said was there, so hopefully we
  // will end up getting something out of this.
  if (payload_len > 0) {
    memcpy(tgt_payload, payload, payload_len);
    clean_cache_range(tgt_payload, payload_len);
  }

  // Finally, toggle the magic number value in order to activate our new header.
  log.magic = next_magic;
  clean_cache_range(&log.magic, sizeof(&log.magic));

  return ZX_OK;
}

zx_status_t ram_crashlog_recover(const void* buf, size_t buf_len,
                                 recovered_ram_crashlog_t* log_out) {
  // We cannot recover a crashlog if there is no place to get the crashlog from,
  // or no place to put the results.
  if ((buf == nullptr) || (log_out == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // We cannot recover a crashlog if the buffer in which the crashlog is
  // supposed to exist is too small.
  if (buf_len < sizeof(ram_crashlog_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // If we don't recognize the magic number here, then the log is too corrupt to
  // even attempt to recover.  Otherwise, the magic number tells us which header
  // is supposed to be active.
  const ram_crashlog_t& log = *(reinterpret_cast<const ram_crashlog_t*>(buf));
  const ram_crashlog_header_t* hdr;
  const uint8_t* payload = reinterpret_cast<const uint8_t*>(&log + 1);

  switch (log.magic) {
    case RAM_CRASHLOG_MAGIC_0:
      hdr = &log.hdr[0];
      break;

    case RAM_CRASHLOG_MAGIC_1:
      hdr = &log.hdr[1];
      break;

    default:
      return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Validate our header CRC.  Like the magic number, if this does not check
  // out, then we cannot recover the log.
  const uint32_t expected_hdr_crc32 = crc32(0, reinterpret_cast<const uint8_t*>(hdr),
                                            offsetof(ram_crashlog_header_t, header_crc32));
  if (expected_hdr_crc32 != hdr->header_crc32) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Things look pretty good at this point.  Fantastic.  Fill out the |log_out|
  // structure.  Do not reject the payload if the length or the CRC fails to
  // pass their sanity checks, just make a note that the payload is not valid
  // and cannot be 100% trusted.
  size_t max_payload = buf_len - sizeof(ram_crashlog_t);
  log_out->uptime = hdr->uptime;
  log_out->reason = hdr->reason;
  if (hdr->payload_len > max_payload) {
    log_out->payload_len = static_cast<uint32_t>(max_payload);
  } else {
    log_out->payload_len = hdr->payload_len;
  }

  log_out->payload = (log_out->payload_len > 0) ? payload : nullptr;
  const uint32_t expected_payload_crc32 =
      crc32(0, reinterpret_cast<const uint8_t*>(log_out->payload), log_out->payload_len);
  log_out->payload_valid =
      (log_out->payload_len == hdr->payload_len) && (expected_payload_crc32 == hdr->payload_crc32);

  return ZX_OK;
}
