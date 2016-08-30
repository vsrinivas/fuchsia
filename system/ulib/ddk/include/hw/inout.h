// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS;

#if defined(__x86_64__) || defined(__i386__)
static inline uint8_t inp(uint16_t _port) {
    uint8_t rv;
    __asm__ __volatile__("inb %1, %0"
                         : "=a"(rv)
                         : "d"(_port));
    return (rv);
}

static inline uint16_t inpw(uint16_t _port) {
    uint16_t rv;
    __asm__ __volatile__("inw %1, %0"
                         : "=a"(rv)
                         : "d"(_port));
    return (rv);
}

static inline uint32_t inpd(uint16_t _port) {
    uint32_t rv;
    __asm__ __volatile__("inl %1, %0"
                         : "=a"(rv)
                         : "d"(_port));
    return (rv);
}

static inline void outp(uint16_t _port, uint8_t _data) {
    __asm__ __volatile__("outb %1, %0"
                         :
                         : "d"(_port),
                           "a"(_data));
}

static inline void outpw(uint16_t _port, uint16_t _data) {
    __asm__ __volatile__("outw %1, %0"
                         :
                         : "d"(_port),
                           "a"(_data));
}

static inline void outpd(uint16_t _port, uint32_t _data) {
    __asm__ __volatile__("outl %1, %0"
                         :
                         : "d"(_port),
                           "a"(_data));
}
#else
static inline uint8_t inp(uint16_t _port) {
    return 0;
}
static inline uint16_t inpw(uint16_t _port) {
    return 0;
}
static inline uint32_t inpd(uint16_t _port) {
    return 0;
}
static inline void outp(uint16_t _port, uint8_t _data) {}
static inline void outpw(uint16_t _port, uint16_t _data) {}
static inline void outpd(uint16_t _port, uint32_t _data) {}
#endif

__END_CDECLS;
