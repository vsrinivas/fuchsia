// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <sys/types.h>
#include <stdio.h>
#include <rand.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/thread.h>
#include <kernel/spinlock.h>
#include <kernel/mutex.h>
#include <platform.h>
#include <arch/ops.h>
#include <inttypes.h>

const size_t BUFSIZE = (1024*1024);
const uint ITER = 1024;

__NO_INLINE static void bench_set_overhead(void)
{
    uint32_t *buf = malloc(BUFSIZE);

    uint64_t count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        __asm__ volatile("");
    }
    count = arch_cycle_count() - count;

    printf("took %" PRIu64 " cycles overhead to loop %u times\n",
           count, ITER);

    free(buf);
}

__NO_INLINE static void bench_memset(void)
{
    uint8_t *buf = memalign(PAGE_SIZE, BUFSIZE);

    uint64_t count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        memset(buf, 0, BUFSIZE);
    }
    count = arch_cycle_count() - count;

    uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
    printf("took %" PRIu64 " cycles to memset a buffer of size %zu %d times (%zu bytes), %llu.%03llu bytes/cycle\n",
           count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

__NO_INLINE static void bench_memset_per_page(void)
{
    uint8_t *buf = memalign(PAGE_SIZE, BUFSIZE);

    uint64_t count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        for (uint j = 0; j < BUFSIZE; j += PAGE_SIZE) {
            memset(buf + j, 0, PAGE_SIZE);
        }
    }
    count = arch_cycle_count() - count;

    uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
    printf("took %" PRIu64 " cycles to per-page memset a buffer of size %zu %d times (%zu bytes), %llu.%03llu bytes/cycle\n",
           count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

__NO_INLINE static void bench_zero_page(void)
{
    uint8_t *buf = memalign(PAGE_SIZE, BUFSIZE);

    uint64_t count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        for (uint j = 0; j < BUFSIZE; j += PAGE_SIZE) {
            arch_zero_page(buf + j);
        }
    }
    count = arch_cycle_count() - count;

    uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
    printf("took %" PRIu64 " cycles to arch_zero_page a buffer of size %zu %d times (%zu bytes), %llu.%03llu bytes/cycle\n",
           count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

#define bench_cset(type) \
__NO_INLINE static void bench_cset_##type(void) \
{ \
    type *buf = malloc(BUFSIZE); \
 \
    uint64_t count = arch_cycle_count(); \
    for (uint i = 0; i < ITER; i++) { \
        for (uint j = 0; j < BUFSIZE / sizeof(*buf); j++) { \
            buf[j] = 0; \
        } \
    } \
    count = arch_cycle_count() - count; \
 \
    uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count; \
    printf("took %" PRIu64 " cycles to clear a buffer using wordsize %d of size %zu %d times (%zu bytes), %llu.%03llu bytes/cycle\n", \
           count, sizeof(*buf), BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000); \
 \
    free(buf); \
}

bench_cset(uint8_t)
bench_cset(uint16_t)
bench_cset(uint32_t)
bench_cset(uint64_t)

__NO_INLINE static void bench_cset_wide(void)
{
    uint32_t *buf = malloc(BUFSIZE);

    uint64_t count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        for (uint j = 0; j < BUFSIZE / sizeof(*buf) / 8; j++) {
            buf[j*8] = 0;
            buf[j*8+1] = 0;
            buf[j*8+2] = 0;
            buf[j*8+3] = 0;
            buf[j*8+4] = 0;
            buf[j*8+5] = 0;
            buf[j*8+6] = 0;
            buf[j*8+7] = 0;
        }
    }
    count = arch_cycle_count() - count;

    uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
    printf("took %" PRIu64 " cycles to clear a buffer of size %zu %d times 8 words at a time (%zu bytes), %llu.%03llu bytes/cycle\n",
           count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

__NO_INLINE static void bench_memcpy(void)
{
    uint8_t *buf = calloc(1, BUFSIZE);

    uint64_t count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        memcpy(buf, buf + BUFSIZE / 2, BUFSIZE / 2);
    }
    count = arch_cycle_count() - count;

    uint64_t bytes_cycle = (BUFSIZE / 2 * ITER * 1000ULL) / count;
    printf("took %" PRIu64 " cycles to memcpy a buffer of size %zu %d times (%zu source bytes), %llu.%03llu source bytes/cycle\n",
           count, BUFSIZE / 2, ITER, BUFSIZE / 2 * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

__NO_INLINE static void bench_spinlock(void)
{
    spin_lock_saved_state_t state;
    spin_lock_saved_state_t state2;
    spin_lock_t lock;

    spin_lock_init(&lock);

#define COUNT (128*1024*1024)
    // test 1: acquire/release a spinlock with interrupts already disabled
    arch_interrupt_save(&state, ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS);

    uint64_t c = arch_cycle_count();
    for (uint i = 0; i < COUNT; i++) {
        spin_lock(&lock);
        spin_unlock(&lock);
    }
    c = arch_cycle_count() - c;

    arch_interrupt_restore(state, ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS);

    printf("%" PRIu64 " cycles to acquire/release spinlock %u times (%" PRIu64 " cycles per)\n", c, COUNT, c / COUNT);

    // test 2: acquire/release a spinlock with irq save and irqs already disabled
    arch_interrupt_save(&state, ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS);

    c = arch_cycle_count();
    for (uint i = 0; i < COUNT; i++) {
        spin_lock_irqsave(&lock, state2);
        spin_unlock_irqrestore(&lock, state2);
    }
    c = arch_cycle_count() - c;

    arch_interrupt_restore(state, ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS);

    printf("%" PRIu64 " cycles to acquire/release spinlock w/irqsave (already disabled) %u times (%" PRIu64 " cycles per)\n", c, COUNT, c / COUNT);

    // test 2: acquire/release a spinlock with irq save and irqs enabled
    c = arch_cycle_count();
    for (uint i = 0; i < COUNT; i++) {
        spin_lock_irqsave(&lock, state2);
        spin_unlock_irqrestore(&lock, state2);
    }
    c = arch_cycle_count() - c;

    printf("%" PRIu64 " cycles to acquire/release spinlock w/irqsave %u times (%" PRIu64 " cycles per)\n", c, COUNT, c / COUNT);
#undef COUNT
}

__NO_INLINE static void bench_mutex(void)
{
    mutex_t m;
    mutex_init(&m);

    static const uint count = 128*1024*1024;
    uint64_t c = arch_cycle_count();
    for (uint i = 0; i < count; i++) {
        mutex_acquire(&m);
        mutex_release(&m);
    }
    c = arch_cycle_count() - c;

    printf("%" PRIu64 " cycles to acquire/release uncontended mutex %u times (%" PRIu64 " cycles per)\n", c, count, c / count);
}

void benchmarks(void)
{
    bench_set_overhead();
    bench_memcpy();
    bench_memset();

    bench_memset_per_page();
    bench_zero_page();

    bench_cset_uint8_t();
    bench_cset_uint16_t();
    bench_cset_uint32_t();
    bench_cset_uint64_t();
    bench_cset_wide();

    bench_spinlock();
    bench_mutex();
}

