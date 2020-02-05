// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_BOOT_CRASH_REASON_H_
#define SYSROOT_ZIRCON_BOOT_CRASH_REASON_H_

#include <stdint.h>
#include <zircon/compiler.h>

// 0 is reserved for "Invalid".  It will never be used by a functioning
// crash-logger.
#define ZIRCON_CRASH_REASON_INVALID ((uint32_t)0)

// "Unknown" indicates that the system does not know the reason for a recent
// crash.  The primary use of this reason is to be something which can be left
// in the crashlog in case the system spontaneously reboots without a chance to
// gracefully finalize the log, perhaps because of something like a hardware
// watchdog timer.
#define ZIRCON_CRASH_REASON_UNKNOWN ((uint32_t)1)

// "No Crash" indicates that the system deliberately rebooted in an
// orderly fashion.  No crash occurred.
#define ZIRCON_CRASH_REASON_NO_CRASH ((uint32_t)2)

// "OOM" indicates a crash triggered by the system because of an unrecoverable
// out-of-memory situation.
#define ZIRCON_CRASH_REASON_OOM ((uint32_t)3)

// "Panic" indicates a crash triggered by the system because of an unrecoverable
// kernel panic situation.
#define ZIRCON_CRASH_REASON_PANIC ((uint32_t)4)

// "Software watchdog" indicates a crash triggered by a kernel level software
// watchdog construct.  Note that this is distinct from a hardware based WDT.
// If the system reboots because of a hardware watchdog, it will have no chance
// to record the reboot reason, and the crashlog will indicate "unknown".  The
// HW reboot reason may be known, but only if the bootloader reports it to us.
#define ZIRCON_CRASH_REASON_SOFTWARE_WATCHDOG ((uint32_t)5)

#ifndef __ASSEMBLER__
#if !__cplusplus
typedef uint32_t zircon_crash_reason_t;
#else   // !__cplusplus
enum class ZirconCrashReason : uint32_t {
  Invalid = ZIRCON_CRASH_REASON_INVALID,
  Unknown = ZIRCON_CRASH_REASON_UNKNOWN,
  NoCrash = ZIRCON_CRASH_REASON_NO_CRASH,
  Oom = ZIRCON_CRASH_REASON_OOM,
  Panic = ZIRCON_CRASH_REASON_PANIC,
  SoftwareWatchdog = ZIRCON_CRASH_REASON_SOFTWARE_WATCHDOG,
};

// Using alias to maintain compatibility with APIs meant to be used by both C and C++.
using zircon_crash_reason_t = ZirconCrashReason;
#endif  // !__cplusplus
#endif  // __ASSEMBLER__

#endif  // SYSROOT_ZIRCON_BOOT_CRASH_REASON_H_
