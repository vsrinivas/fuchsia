// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// All code doing MMIO access must go through this API rather than using direct
// pointer dereferences.

#ifndef SRC_LIB_DDK_INCLUDE_HW_REG_H_
#define SRC_LIB_DDK_INCLUDE_HW_REG_H_

#include <stdint.h>

#ifdef __aarch64__
// The Linux/ARM64 KVM hypervisor does not support MMIO access via load/store
// instructions that use writeback, which the compiler might decide to generate.
// (The ARM64 virtualization hardware requires software assistance for the
// writeback forms but not for the non-writeback forms, and KVM just doesn't
// bother to implement that software assistance.)  To minimize the demands on a
// hypervisor we might run under, we use inline assembly definitions here to
// ensure that only the non-writeback load/store instructions are used.

static inline void writeb(uint8_t v, volatile void* a) {
  __asm__ volatile("strb %w1, %0" : "=m"(*(volatile uint8_t*)a) : "r"(v));
}
static inline void writew(uint16_t v, volatile void* a) {
  __asm__ volatile("strh %w1, %0" : "=m"(*(volatile uint16_t*)a) : "r"(v));
}
static inline void writel(uint32_t v, volatile void* a) {
  __asm__ volatile("str %w1, %0" : "=m"(*(volatile uint32_t*)a) : "r"(v));
}
static inline void writell(uint64_t v, volatile void* a) {
  __asm__ volatile("str %1, %0" : "=m"(*(volatile uint64_t*)a) : "r"(v));
}

static inline uint8_t readb(const volatile void* a) {
  uint8_t v;
  __asm__ volatile("ldrb %w0, %1" : "=r"(v) : "m"(*(volatile uint8_t*)a));
  return v;
}
static inline uint16_t readw(const volatile void* a) {
  uint16_t v;
  __asm__ volatile("ldrh %w0, %1" : "=r"(v) : "m"(*(volatile uint16_t*)a));
  return v;
}
static inline uint32_t readl(const volatile void* a) {
  uint32_t v;
  __asm__ volatile("ldr %w0, %1" : "=r"(v) : "m"(*(volatile uint32_t*)a));
  return v;
}
static inline uint64_t readll(const volatile void* a) {
  uint64_t v;
  __asm__ volatile("ldr %0, %1" : "=r"(v) : "m"(*(volatile uint64_t*)a));
  return v;
}

#else
// TODO(fxbug.dev/12611): Similar to arm64 above, the Fuchsia hypervisor's instruction decoder does not
// support MMIO access via load/store instructions that use writeback, which the compiler may
// generate. Until support is implemented, we use inline assembly definitions here to ensure that
// only the non-writeback move instructions are used.

static inline void writeb(uint8_t v, volatile void* a) {
  __asm__ volatile("movb %1, %0" : "=m"(*(volatile uint8_t*)a) : "r"(v));
}
static inline void writew(uint16_t v, volatile void* a) {
  __asm__ volatile("movw %1, %0" : "=m"(*(volatile uint16_t*)a) : "r"(v));
}
static inline void writel(uint32_t v, volatile void* a) {
  __asm__ volatile("movl %1, %0" : "=m"(*(volatile uint32_t*)a) : "r"(v));
}
static inline void writell(uint64_t v, volatile void* a) {
  __asm__ volatile("movq %1, %0" : "=m"(*(volatile uint64_t*)a) : "r"(v));
}

static inline uint8_t readb(const volatile void* a) {
  uint8_t v;
  __asm__ volatile("movb %1, %0" : "=r"(v) : "m"(*(volatile uint8_t*)a));
  return v;
}
static inline uint16_t readw(const volatile void* a) {
  uint16_t v;
  __asm__ volatile("movw %w1, %0" : "=r"(v) : "m"(*(volatile uint16_t*)a));
  return v;
}
static inline uint32_t readl(const volatile void* a) {
  uint32_t v;
  __asm__ volatile("movl %1, %0" : "=r"(v) : "m"(*(volatile uint32_t*)a));
  return v;
}
static inline uint64_t readll(const volatile void* a) {
  uint64_t v;
  __asm__ volatile("movq %1, %0" : "=r"(v) : "m"(*(volatile uint64_t*)a));
  return v;
}

#endif

#define RMWREG8(addr, startbit, width, val) \
  writeb((readb(addr) & ~(((1 << (width)) - 1) << (startbit))) | ((val) << (startbit)), (addr))
#define RMWREG16(addr, startbit, width, val) \
  writew((readw(addr) & ~(((1 << (width)) - 1) << (startbit))) | ((val) << (startbit)), (addr))
#define RMWREG32(addr, startbit, width, val) \
  writel((readl(addr) & ~(((1 << (width)) - 1) << (startbit))) | ((val) << (startbit)), (addr))
#define RMWREG64(addr, startbit, width, val) \
  writell((readll(addr) & ~(((1ull << (width)) - 1) << (startbit))) | ((val) << (startbit)), (addr))

#define set_bitsb(v, a) writeb(readb(a) | (v), (a))
#define clr_bitsb(v, a) writeb(readb(a) & (uint8_t) ~(v), (a))

#define set_bitsw(v, a) writew(readw(a) | (v), (a))
#define clr_bitsw(v, a) writew(readw(a) & (uint16_t) ~(v), (a))

#define set_bitsl(v, a) writel(readl(a) | (v), (a))
#define clr_bitsl(v, a) writel(readl(a) & (uint32_t) ~(v), (a))

#define set_bitsll(v, a) writell(readll(a) | (v), (a))
#define clr_bitsll(v, a) writell(readll(a) & (uint64_t) ~(v), (a))

#endif  // SRC_LIB_DDK_INCLUDE_HW_REG_H_
