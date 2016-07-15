// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdint.h>

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
