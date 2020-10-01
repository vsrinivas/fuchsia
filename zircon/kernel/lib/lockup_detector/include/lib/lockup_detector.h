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

// Initialize the lockup detector.
//
// This should be called once on CPU-0, before we've gone SMP, but after the platform timer has been
// initialized since it needs to perform ticks to time conversion.
void lockup_init(void);

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

__END_CDECLS

#ifdef __cplusplus

#include <ktl/atomic.h>

// Per CPU state for lockup detector.
struct LockupDetectorState {
  // The time (tick count) at which the CPU entered the critical section.
  ktl::atomic<zx_ticks_t> begin_ticks = 0;
  // Critical sections may be nested so must keep track of the depth.
  uint32_t critical_section_depth = 0;
};

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_H_
