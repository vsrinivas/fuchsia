// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <magenta/ktrace.h>

__BEGIN_CDECLS

#if WITH_LIB_KTRACE
void* ktrace_open(uint32_t tag);
void ktrace_tiny(uint32_t tag, uint32_t arg);
static inline void ktrace(uint32_t tag, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t* data = (uint32_t*) ktrace_open(tag);
    if (data) {
        data[0] = a; data[1] = b; data[2] = c; data[3] = d;
    }
}
void ktrace_name(uint32_t tag, uint32_t id, uint32_t arg, const char* name);
int ktrace_read_user(void* ptr, uint32_t off, uint32_t len);
status_t ktrace_control(uint32_t action, uint32_t options);
#else
static inline void* ktrace_open(uint32_t tag) { return NULL; }
static inline void ktrace_tiny(uint32_t tag, uint32_t arg) {}
static inline void ktrace(uint32_t tag, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {}
static inline void ktrace_name(uint32_t tag, uint32_t id, uint32_t arg, const char* name) {}
static inline ssize_t ktrace_read_user(void* ptr, uint32_t off, uint32_t len) {
    if ((len == 0) && (off == 0)) {
        return 0;
    } else {
        return ERR_INVALID_ARGS;
    }
}
static inline status_t ktrace_control(uint32_t action, uint32_t options) {
    return ERR_NOT_SUPPORTED;
}
#endif

#define KTRACE_DEFAULT_BUFSIZE 32 // MB
#define KTRACE_DEFAULT_GRPMASK 0xFFF

__END_CDECLS