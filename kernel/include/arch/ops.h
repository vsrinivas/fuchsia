// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/* #defines for the cache routines below */
#define ICACHE 1
#define DCACHE 2
#define UCACHE (ICACHE|DCACHE)

#ifndef ASSEMBLY

#include <arch/defines.h>
#include <kernel/atomic.h>
#include <magenta/compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

__BEGIN_CDECLS

/* fast routines that most arches will implement inline */
static void arch_enable_ints(void);
static void arch_disable_ints(void);
static bool arch_ints_disabled(void);
static bool arch_in_int_handler(void);

static uint64_t arch_cycle_count(void);

static uint arch_curr_cpu_num(void);
static uint arch_max_num_cpus(void);

/* Use to align structures on cache lines to avoid cpu aliasing. */
#define __CPU_ALIGN __ALIGNED(CACHE_LINE)
#define __CPU_MAX_ALIGN __ALIGNED(MAX_CACHE_LINE)

void arch_disable_cache(uint flags);
void arch_enable_cache(uint flags);

void arch_clean_cache_range(addr_t start, size_t len);
void arch_clean_invalidate_cache_range(addr_t start, size_t len);
void arch_invalidate_cache_range(addr_t start, size_t len);
void arch_sync_cache_range(addr_t start, size_t len);

/* Used to suspend work on a CPU until it is further shutdown.
 * This will only be invoked with interrupts disabled.  This function
 * must not re-enter the scheduler.
 * flush_done should be signaled after state is flushed. */
typedef struct event event_t;
void arch_flush_state_and_halt(event_t *flush_done) __NO_RETURN;

void arch_idle(void);

/* function to call in spinloops to idle */
static void arch_spinloop_pause(void);
/* function to call when an event happens that may trigger the exit from
 * a spinloop */
static void arch_spinloop_signal(void);

/* arch optimized version of a page zero routine against a page aligned buffer */
void arch_zero_page(void *);

/* give the specific arch a chance to override some routines */
#include <arch/arch_ops.h>

__END_CDECLS

#endif // !ASSEMBLY
