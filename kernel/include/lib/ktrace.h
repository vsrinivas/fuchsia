// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <magenta/compiler.h>
#include <magenta/ktrace.h>

__BEGIN_CDECLS

#if WITH_LIB_KTRACE

typedef struct ktrace_probe_info ktrace_probe_info_t;

struct ktrace_probe_info {
    ktrace_probe_info_t* next;
    const char* name;
    uint32_t num;
} __ALIGNED(16); // align on multiple of 16 to match linker packing of the ktrace_probe section

void* ktrace_open(uint32_t tag);
void ktrace_tiny(uint32_t tag, uint32_t arg);
static inline void ktrace(uint32_t tag, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t* data = (uint32_t*) ktrace_open(tag);
    if (data) {
        data[0] = a; data[1] = b; data[2] = c; data[3] = d;
    }
}
#define ktrace_probe0(_name) { \
    static __SECTION("ktrace_probe") ktrace_probe_info_t info = { .name = _name }; \
    ktrace_open(TAG_PROBE_16(info.num)); \
}
#define ktrace_probe2(_name,arg0,arg1) { \
    static __SECTION("ktrace_probe") ktrace_probe_info_t info = { .name = _name }; \
    uint32_t* args = ktrace_open(TAG_PROBE_24(info.num)); \
    if (args) { \
        args[0] = arg0; \
        args[1] = arg1; \
    } \
}
void ktrace_name(uint32_t tag, uint32_t id, uint32_t arg, const char* name);
int ktrace_read_user(void* ptr, uint32_t off, uint32_t len);
status_t ktrace_control(uint32_t action, uint32_t options, void* ptr);
#else
static inline void* ktrace_open(uint32_t tag) { return NULL; }
static inline void ktrace_tiny(uint32_t tag, uint32_t arg) {}
static inline void ktrace(uint32_t tag, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {}
static inline void ktrace_probe0(const char* name) {}
static inline void ktrace_probe2(const char* name, uint32_t arg0, uint32_t arg1) {}
static inline void ktrace_name(uint32_t tag, uint32_t id, uint32_t arg, const char* name) {}
static inline ssize_t ktrace_read_user(void* ptr, uint32_t off, uint32_t len) {
    if ((len == 0) && (off == 0)) {
        return 0;
    } else {
        return ERR_INVALID_ARGS;
    }
}
static inline status_t ktrace_control(uint32_t action, uint32_t options, void* ptr) {
    return ERR_NOT_SUPPORTED;
}
#endif

#define KTRACE_DEFAULT_BUFSIZE 32 // MB
#define KTRACE_DEFAULT_GRPMASK 0xFFF

void ktrace_report_live_threads(void);

__END_CDECLS
