#pragma once

#include <stdint.h>

#include "atomic_arch.h"

#ifdef a_ll

#ifndef a_swap
#define a_swap a_swap
static inline int a_swap(volatile int* p, int v) {
    int old;
    do
        old = a_ll(p);
    while (!a_sc(p, v));
    return old;
}
#endif

#ifndef a_fetch_add
#define a_fetch_add a_fetch_add
static inline int a_fetch_add(volatile int* p, int v) {
    int old;
    do
        old = a_ll(p);
    while (!a_sc(p, (unsigned)old + v));
    return old;
}
#endif

#endif

#ifndef a_cas
#error missing definition of a_cas
#endif

#ifndef a_fetch_and
#define a_fetch_and a_fetch_and
static inline int a_fetch_and(volatile int* p, int v) {
    int old;
    do
        old = *p;
    while (a_cas(p, old, old & v) != old);
    return old;
}
#endif

#ifndef a_fetch_or
#define a_fetch_or a_fetch_or
static inline int a_fetch_or(volatile int* p, int v) {
    int old;
    do
        old = *p;
    while (a_cas(p, old, old | v) != old);
    return old;
}
#endif

#ifndef a_and
#define a_and a_and
static inline void a_and(volatile int* p, int v) {
    a_fetch_and(p, v);
}
#endif

#ifndef a_or
#define a_or a_or
static inline void a_or(volatile int* p, int v) {
    a_fetch_or(p, v);
}
#endif

#ifndef a_inc
#define a_inc a_inc
static inline void a_inc(volatile int* p) {
    a_fetch_add(p, 1);
}
#endif

#ifndef a_dec
#define a_dec a_dec
static inline void a_dec(volatile int* p) {
    a_fetch_add(p, -1);
}
#endif

#ifndef a_store
#define a_store a_store
static inline void a_store(volatile int* p, int v) {
    atomic_thread_fence(memory_order_seq_cst);
    *p = v;
    atomic_thread_fence(memory_order_seq_cst);
}
#endif

#ifndef a_spin
#define a_spin() atomic_thread_fence(memory_order_seq_cst)
#endif

#ifndef a_and_64
#define a_and_64 a_and_64
static inline void a_and_64(volatile uint64_t* p, uint64_t v) {
    union {
        uint64_t v;
        uint32_t r[2];
    } u = {v};
    if (u.r[0] + 1)
        a_and((int*)p, u.r[0]);
    if (u.r[1] + 1)
        a_and((int*)p + 1, u.r[1]);
}
#endif

#ifndef a_or_64
#define a_or_64 a_or_64
static inline void a_or_64(volatile uint64_t* p, uint64_t v) {
    union {
        uint64_t v;
        uint32_t r[2];
    } u = {v};
    if (u.r[0])
        a_or((int*)p, u.r[0]);
    if (u.r[1])
        a_or((int*)p + 1, u.r[1]);
}
#endif

#ifndef a_or_l
#define a_or_l a_or_l
static inline void a_or_l(volatile void* p, long v) {
    if (sizeof(long) == sizeof(int))
        a_or(p, v);
    else
        a_or_64(p, v);
}
#endif
