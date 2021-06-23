// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/process.h>
#include <zircon/syscalls.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include <zxtest/zxtest.h>

#if defined(__x86_64__)

namespace {

struct far_jmp {
  uint32_t offset;
  uint16_t segsel;
} __attribute__((packed));

#define MOV_INTO_SREG(sreg, segsel) __asm__ volatile("mov %0, %%" #sreg : : "rm"(segsel));

inline void jmp_far(uint16_t segsel) {
  far_jmp dest = {.offset = 0, .segsel = segsel};
  __asm__ volatile("ljmp *%[dest]\n" : : [dest] "m"(dest));
}

constexpr uint16_t lastGDTEntry = 0xfffb;   // Index: 8191, RPL: 3, Table Indicator: GDT
constexpr uint16_t firstLDTEntry = 0x0007;  // Index: 0, RPL: 3, Table Indicator: LDT

TEST(BadSegselTest, LoadLastGDTEntry) {
  ASSERT_DEATH([]() { MOV_INTO_SREG(ds, lastGDTEntry) });
  ASSERT_DEATH([]() { MOV_INTO_SREG(ss, lastGDTEntry) });
  ASSERT_DEATH([]() { MOV_INTO_SREG(es, lastGDTEntry) });
  ASSERT_DEATH([]() { MOV_INTO_SREG(gs, lastGDTEntry) });
  ASSERT_DEATH([]() { MOV_INTO_SREG(fs, lastGDTEntry) });
}

TEST(BadSegselTest, LoadFirstLDTEntry) {
  ASSERT_DEATH([]() { MOV_INTO_SREG(ds, firstLDTEntry) });
  ASSERT_DEATH([]() { MOV_INTO_SREG(ss, firstLDTEntry) });
  ASSERT_DEATH([]() { MOV_INTO_SREG(es, firstLDTEntry) });
  ASSERT_DEATH([]() { MOV_INTO_SREG(gs, firstLDTEntry) });
  ASSERT_DEATH([]() { MOV_INTO_SREG(fs, firstLDTEntry) });
}

TEST(BadSegselTest, JumpToLastGDTEntry) {
  ASSERT_DEATH([]() { jmp_far(lastGDTEntry); });
}

TEST(BadSegselTest, JumpToFirstLDTEntry) {
  ASSERT_DEATH([]() { jmp_far(firstLDTEntry); });
}

TEST(BadSegselTest, TestAllGDTSelectors) {
  // Fix RPL = 3, T/I = 0, and iterate over the remaining 13 bits.
  for (uint32_t i = 3; i < 0xFFFF; i += 0x8) {
    uint32_t access;
    __asm__ volatile("larl %[selector], %[access]" : [access] "=r"(access) : [selector] "rm"(i));
  }
}

// The int instruction takes an immediate value, so we have to generate
// all possible 256 instructions combinations.
template <int N>
inline void TestInterrupt();

template <>
inline void TestInterrupt<-1>() {}

template <int N>
inline void TestInterrupt() {
  ASSERT_DEATH([]() { __asm__ volatile("int %0" ::"i"(N)); });
  TestInterrupt<N - 1>();
}

// Test that executing "int x" crashes for all numbers in [0, 255].
TEST(TestInterrupt, TestIntCrashes) { TestInterrupt<255>(); }

}  // namespace

#endif  // defined(__x86_64__)
