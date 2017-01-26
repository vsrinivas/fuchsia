// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <sys/types.h>
#include <stdio.h>
#include <rand.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <app/tests.h>
#include <kernel/thread.h>
#include <kernel/mutex.h>
#include <kernel/semaphore.h>
#include <kernel/event.h>
#include <platform.h>
#include <arch/ops.h>

const size_t BUFSIZE = (1024*1024);
const uint ITER = 1024;

__NO_INLINE static void bench_set_overhead(void)
{
    uint32_t *buf = malloc(BUFSIZE);

    uint count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        __asm__ volatile("");
    }
    count = arch_cycle_count() - count;

    printf("took %u cycles overhead to loop %u times\n",
           count, ITER);

    free(buf);
}

__NO_INLINE static void bench_memset(void)
{
    uint8_t *buf = memalign(PAGE_SIZE, BUFSIZE);

    uint count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        memset(buf, 0, BUFSIZE);
    }
    count = arch_cycle_count() - count;

    uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
    printf("took %u cycles to memset a buffer of size %u %d times (%u bytes), %llu.%03llu bytes/cycle\n",
           count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

__NO_INLINE static void bench_memset_per_page(void)
{
    uint8_t *buf = memalign(PAGE_SIZE, BUFSIZE);

    uint count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        for (uint j = 0; j < BUFSIZE; j += PAGE_SIZE) {
            memset(buf + j, 0, PAGE_SIZE);
        }
    }
    count = arch_cycle_count() - count;

    uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
    printf("took %u cycles to per-page memset a buffer of size %u %d times (%u bytes), %llu.%03llu bytes/cycle\n",
           count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

__NO_INLINE static void bench_zero_page(void)
{
    uint8_t *buf = memalign(PAGE_SIZE, BUFSIZE);

    uint count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        for (uint j = 0; j < BUFSIZE; j += PAGE_SIZE) {
            arch_zero_page(buf + j);
        }
    }
    count = arch_cycle_count() - count;

    uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
    printf("took %u cycles to arch_zero_page a buffer of size %u %d times (%u bytes), %llu.%03llu bytes/cycle\n",
           count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

#define bench_cset(type) \
__NO_INLINE static void bench_cset_##type(void) \
{ \
    type *buf = malloc(BUFSIZE); \
 \
    uint count = arch_cycle_count(); \
    for (uint i = 0; i < ITER; i++) { \
        for (uint j = 0; j < BUFSIZE / sizeof(*buf); j++) { \
            buf[j] = 0; \
        } \
    } \
    count = arch_cycle_count() - count; \
 \
    uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count; \
    printf("took %u cycles to manually clear a buffer using wordsize %d of size %u %d times (%u bytes), %llu.%03llu bytes/cycle\n", \
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

    uint count = arch_cycle_count();
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
    printf("took %u cycles to manually clear a buffer of size %u %d times 8 words at a time (%u bytes), %llu.%03llu bytes/cycle\n",
           count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

__NO_INLINE static void bench_memcpy(void)
{
    uint8_t *buf = calloc(1, BUFSIZE);

    uint count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        memcpy(buf, buf + BUFSIZE / 2, BUFSIZE / 2);
    }
    count = arch_cycle_count() - count;

    uint64_t bytes_cycle = (BUFSIZE / 2 * ITER * 1000ULL) / count;
    printf("took %u cycles to memcpy a buffer of size %u %d times (%u source bytes), %llu.%03llu source bytes/cycle\n",
           count, BUFSIZE / 2, ITER, BUFSIZE / 2 * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

    free(buf);
}

#if WITH_LIB_LIBM && !WITH_NO_FP
#include <math.h>

__NO_INLINE static void bench_sincos(void)
{
    printf("touching the floating point unit\n");
    __UNUSED volatile double _hole = sin(0);

    uint count = arch_cycle_count();
    __UNUSED double a = sin(2.0);
    count = arch_cycle_count() - count;
    printf("took %u cycles for sin()\n", count);

    count = arch_cycle_count();
    a = cos(2.0);
    count = arch_cycle_count() - count;
    printf("took %u cycles for cos()\n", count);

    count = arch_cycle_count();
    a = sinf(2.0);
    count = arch_cycle_count() - count;
    printf("took %u cycles for sinf()\n", count);

    count = arch_cycle_count();
    a = cosf(2.0);
    count = arch_cycle_count() - count;
    printf("took %u cycles for cosf()\n", count);

    count = arch_cycle_count();
    a = sqrt(1234567.0);
    count = arch_cycle_count() - count;
    printf("took %u cycles for sqrt()\n", count);

    count = arch_cycle_count();
    a = sqrtf(1234567.0f);
    count = arch_cycle_count() - count;
    printf("took %u cycles for sqrtf()\n", count);
}

#endif // WITH_LIB_LIBM

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

#if WITH_LIB_LIBM && !WITH_NO_FP
    bench_sincos();
#endif
}

