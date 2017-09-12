// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <zircon/syscalls.h>
#include <unittest/unittest.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

struct desc_ptr {
    unsigned short size;
    unsigned long address;
} __attribute__((packed)) ;

bool processor_test(void) {
    BEGIN_TEST;

#if defined(__x86_64__)
    // Check the IDT is not in the kernel module
    // TODO(thgarnie) check all CPUs when sched_setaffinity is implemented
    struct desc_ptr idt;
    __asm__ ("sidt %0" : "=m" (idt));
    unittest_printf("IDT address = %lx\n", idt.address);
    EXPECT_LT(idt.address, 0xffffffff80000000UL,
              "Check IDT is not in the kernel module (remapped)");
#endif

    END_TEST;
}

BEGIN_TEST_CASE(processor_tests)
RUN_TEST(processor_test)
END_TEST_CASE(processor_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
