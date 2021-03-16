// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_H_
#define ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_H_

#include <assert.h>
#include <lib/lockup_detector/inline_impl.h>
#include <zircon/time.h>

// Documentation for this library can be found in kernel/lib/lockup_detector/README.md.

// Initialize the lockup detector for the primary CPU.
//
// This should be called once on the boot CPU (|BOOT_CPU_ID|), before we've gone SMP, but after the
// platform timer has been initialized since it needs to perform ticks to time conversion.
void lockup_primary_init();

// Initialize the lockup detector for a secondary CPU.
//
// This should be called once on each secondary CPU after the platform timer has been initialized.
void lockup_secondary_init();

// Shutdown the lockup detector for a secondary CPU.
//
// This should be called once on each secondary CPU prior to taking it offline.
void lockup_secondary_shutdown();

// Accessors exposed for testing.
zx_ticks_t lockup_get_cs_threshold_ticks();
void lockup_set_cs_threshold_ticks(zx_ticks_t ticks);

#define LOCKUP_CRITICAL_SECTION_ENALBED 1

#if LOCKUP_CRITICAL_SECTION_ENALBED
#define LOCKUP_BEGIN(name) \
  do {                     \
    lockup_begin(name);    \
  } while (false)
#define LOCKUP_END() \
  do {               \
    lockup_end();    \
  } while (false)
#define LOCKUP_TIMED_BEGIN(name) \
  do {                           \
    lockup_timed_begin(name);    \
  } while (false)
#define LOCKUP_TIMED_END() \
  do {                     \
    lockup_timed_end();    \
  } while (false)
#else
#define LOCKUP_BEGIN(name)
#define LOCKUP_END()
#define LOCKUP_TIMED_BEGIN(name)
#define LOCKUP_TIMED_END()
#endif

// Used to indicate the CPU is entering a critical section with name |name| where it might appear to
// be locked up.
//
// |name| must have static lifetime.
//
// Must be called with preemption disabled or interrupts disabled.
//
// Do not use directly.  Use |LOCKUP_BEGIN| macro instead.
inline void lockup_begin(const char* name);

// Used to indicate the CPU has left a critical section.
//
// Must be called with preemption disabled or interrupts disabled.
//
// Do not use directly.  Use |LOCKUP_END| macro instead.
inline void lockup_end();

// Same as lockup_begin except the critical section is timed.
void lockup_timed_begin(const char* name);

// Same as lockup_end except the critical section is timed.
void lockup_timed_end();

// Returns the number of times a "critical section threshold exceeded" oops was triggered.
int64_t lockup_get_critical_section_oops_count();

// Returns the number of times a "no heartbeat" oops was triggered.
int64_t lockup_get_no_heartbeat_oops_count();

#endif  // ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_H_
