// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_H_
#define ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_H_

#include <assert.h>
#include <zircon/time.h>

// Documentation for this library can be found in kernel/lib/lockup_detector/README.md.

// TODO(fxbug.dev/59395): This header is still included (transitively) by some C code.  Once the
// dependency is eliminated, drop the __BEGIN_CDECLS/__END_CDECLS/__cplusplus guards.
__BEGIN_CDECLS

// Initialize the lockup detector for the primary CPU.
//
// This should be called once on the boot CPU (|BOOT_CPU_ID|), before we've gone SMP, but after the
// platform timer has been initialized since it needs to perform ticks to time conversion.
void lockup_primary_init(void);

// Initialize the lockup detector for a secondary CPU.
//
// This should be called once on each secondary CPU after the platform timer has been initialized.
void lockup_secondary_init(void);

// Shutdown the lockup detector for a secondary CPU.
//
// This should be called once on each secondary CPU prior to taking it offline.
void lockup_secondary_shutdown(void);

// Accessors exposed for testing.
zx_ticks_t lockup_get_cs_threshold_ticks(void);
void lockup_set_cs_threshold_ticks(zx_ticks_t ticks);

#if DEBUG_ASSERT_IMPLEMENTED
#define LOCKUP_BEGIN() \
  do {                 \
    lockup_begin();    \
  } while (false)
#define LOCKUP_END() \
  do {               \
    lockup_end();    \
  } while (false)
#else
#define LOCKUP_BEGIN()
#define LOCKUP_END()
#endif

// Used to indicate the CPU is entering a critical section where it might appear to be locked up.
//
// Must be called with interrupts disabled.
//
// Do not use directly.  Use |LOCKUP_BEGIN| macro instead.
void lockup_begin(void);

// Used to indicate the CPU has left a critical section.
//
// Must be called with interrupts disabled.
//
// Do not use directly.  Use |LOCKUP_END| macro instead.
void lockup_end(void);

// Returns the number of times a "critical section threshold exceeded" oops was triggered.
int64_t lockup_get_critical_section_oops_count(void);

// Returns the number of times a "no heartbeat" oops was triggered.
int64_t lockup_get_no_heartbeat_oops_count(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_H_
