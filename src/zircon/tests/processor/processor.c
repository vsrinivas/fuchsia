// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#if defined(__x86_64__)
#include <cpuid.h>
#endif
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

struct desc_ptr {
  unsigned short size;
  unsigned long address;
} __attribute__((packed));

#if defined(__x86_64__)
static bool is_umip_supported(void) {
  uint32_t eax, ebx, ecx, edx;
  if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) != 1) {
    return false;
  }
  return ecx & (1u << 2);
}
#endif

#if defined(__x86_64__)
// Test that the IDT visible via SIDT has been remapped
TEST(ProcessorTestCase, IdtRelocated) {
  if (!is_umip_supported()) {
    // Check the IDT is not in the kernel module.  Only run this check if
    // UMIP is not supported, since otherwise this will fault.

    // TODO(thgarnie) check all CPUs when sched_setaffinity is implemented
    struct desc_ptr idt;
    __asm__("sidt %0" : "=m"(idt));
    EXPECT_LT(idt.address, 0xffffffff80000000UL,
              "Check IDT is not in the kernel module (remapped)");
  }
}
#endif
