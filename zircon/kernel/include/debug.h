// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_DEBUG_H_
#define ZIRCON_KERNEL_INCLUDE_DEBUG_H_

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <zircon/compiler.h>

#include <platform/debug.h>

#if !defined(LK_DEBUGLEVEL)
#define LK_DEBUGLEVEL 0
#endif

/* debug levels */
#define CRITICAL 0
#define ALWAYS 0
#define INFO 1
#define SPEW 2

__BEGIN_CDECLS

typedef int(hexdump_print_fn_t)(const char *fmt, ...);

#if !DISABLE_DEBUG_OUTPUT

/* dump memory */
void hexdump_very_ex(const void *ptr, size_t len, uint64_t disp_addr_start,
                     hexdump_print_fn_t *pfn);
void hexdump8_very_ex(const void *ptr, size_t len, uint64_t disp_addr_start,
                      hexdump_print_fn_t *pfn);

#else

/* Obtain the panic file descriptor. */
static inline FILE *get_panic_fd(void) { return NULL; }

/* dump memory */
static inline void hexdump_very_ex(const void *ptr, size_t len, uint64_t disp_addr_start,
                                   hexdump_print_fn_t *pfn) {}
static inline void hexdump8_very_ex(const void *ptr, size_t len, uint64_t disp_addr_start,
                                    hexdump_print_fn_t *pfn) {}

#endif /* DISABLE_DEBUG_OUTPUT */

static inline void hexdump_ex(const void *ptr, size_t len, uint64_t disp_addr_start) {
  hexdump_very_ex(ptr, len, disp_addr_start, _printf);
}

static inline void hexdump8_ex(const void *ptr, size_t len, uint64_t disp_addr_start) {
  hexdump8_very_ex(ptr, len, disp_addr_start, _printf);
}

static inline void hexdump(const void *ptr, size_t len) {
  hexdump_ex(ptr, len, (uint64_t)((vaddr_t)ptr));
}

static inline void hexdump8(const void *ptr, size_t len) {
  hexdump8_ex(ptr, len, (uint64_t)((vaddr_t)ptr));
}

#define dprintf(level, x...)        \
  do {                              \
    if ((level) <= LK_DEBUGLEVEL) { \
      printf(x);                    \
    }                               \
  } while (0)

/* systemwide halts */
void _panic(void *caller, void *frame, const char *fmt, ...) __PRINTFLIKE(3, 4) __NO_RETURN;

#define panic(x...) _panic(__GET_CALLER(), __GET_FRAME(), x)

void _panic_no_format(const char *msg, size_t len) __NO_RETURN;

__NO_RETURN static inline void panic_no_format(const char *msg) {
  _panic_no_format(msg, __builtin_strlen(msg));
}

#define PANIC_UNIMPLEMENTED panic("%s unimplemented\n", __PRETTY_FUNCTION__)

void __stack_chk_fail(void) __NO_RETURN;

uintptr_t choose_stack_guard(void);

/* spin the cpu for a period of (short) time */
void spin(uint32_t usecs);

/* spin the cpu for a certain number of cpu cycles */
void spin_cycles(uint32_t usecs);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_DEBUG_H_
