// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <ktl/unique_ptr.h>

#include "interrupt_manager.h"

namespace {

class FakeIoApic {
private:
    // Make sure we can have more interrupts than CPU vectors, so we can test
    // too many allocations
    static constexpr unsigned int kIrqCount = X86_INT_COUNT + 1;
public:
    static bool IsValidInterrupt(unsigned int vector, uint32_t flags) {
        return vector < kIrqCount;
    }
    static uint8_t FetchIrqVector(unsigned int vector) {
        ZX_ASSERT(vector < kIrqCount);
        return FakeIoApic::entries[vector].x86_vector;
    }
    static void ConfigureIrqVector(uint32_t global_irq, uint8_t x86_vector) {
        ZX_ASSERT(global_irq < kIrqCount);
        FakeIoApic::entries[global_irq].x86_vector = x86_vector;
    }
    static void ConfigureIrq(uint32_t global_irq, enum interrupt_trigger_mode trig_mode,
                             enum interrupt_polarity polarity,
                             enum apic_interrupt_delivery_mode del_mode, bool mask,
                             enum apic_interrupt_dst_mode dst_mode, uint8_t dst, uint8_t vector) {
        ZX_ASSERT(global_irq < kIrqCount);
        FakeIoApic::entries[global_irq].x86_vector = vector;
        FakeIoApic::entries[global_irq].trig_mode = trig_mode;
        FakeIoApic::entries[global_irq].polarity = polarity;
    }
    static void MaskIrq(uint32_t global_irq, bool mask) {
        ZX_ASSERT(global_irq < kIrqCount);
    }
    static zx_status_t FetchIrqConfig(uint32_t global_irq, enum interrupt_trigger_mode* trig_mode,
                                      enum interrupt_polarity* polarity) {
        ZX_ASSERT(global_irq < kIrqCount);
        *trig_mode = FakeIoApic::entries[global_irq].trig_mode;
        *polarity = FakeIoApic::entries[global_irq].polarity;
        return ZX_OK;
    }

    // Reset the internal state
    static void Reset() {
        for (auto& entry : entries) {
            entry = {};
        }
    }

    struct Entry {
        uint8_t x86_vector;
        enum interrupt_trigger_mode trig_mode;
        enum interrupt_polarity polarity;
    };
    static Entry entries[kIrqCount];
};
FakeIoApic::Entry FakeIoApic::entries[kIrqCount] = {};

bool TestRegisterInterruptHandler() {
    BEGIN_TEST;

    FakeIoApic::Reset();
    fbl::AllocChecker ac;
    auto im = ktl::make_unique<InterruptManager<FakeIoApic>>(&ac);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_EQ(im->Init(), ZX_OK, "");

    unsigned int kIrq1 = 1;
    uint8_t handler1_arg = 0;
    uintptr_t handler1 = 2;

    // Register a handler for the interrupt
    ASSERT_EQ(im->RegisterInterruptHandler(kIrq1, reinterpret_cast<int_handler>(handler1),
                                          &handler1_arg), ZX_OK, "");
    uint8_t irq1_x86_vector = FakeIoApic::entries[kIrq1].x86_vector;

    // Make sure the entry matches
    int_handler handler;
    void* arg;
    im->GetEntryByX86Vector(irq1_x86_vector, &handler, &arg);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(handler), handler1, "");
    EXPECT_EQ(arg, &handler1_arg, "");

    // Unregister it
    ASSERT_EQ(im->RegisterInterruptHandler(kIrq1, nullptr, nullptr), ZX_OK, "");
    EXPECT_EQ(FakeIoApic::entries[kIrq1].x86_vector, 0, "");
    // Make sure the entry matches
    im->GetEntryByX86Vector(irq1_x86_vector, &handler, &arg);
    EXPECT_EQ(handler, nullptr, "");
    EXPECT_EQ(arg, nullptr, "");

    END_TEST;
}

bool TestUnregisterInterruptHandlerNotRegistered() {
    BEGIN_TEST;

    FakeIoApic::Reset();
    fbl::AllocChecker ac;
    auto im = ktl::make_unique<InterruptManager<FakeIoApic>>(&ac);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_EQ(im->Init(), ZX_OK, "");

    unsigned int kIrq1 = 1;

    // Unregister a vector we haven't yet registered.  Should just be ignored.
    ASSERT_EQ(im->RegisterInterruptHandler(kIrq1, nullptr, nullptr), ZX_OK, "");

    END_TEST;
}

bool TestRegisterInterruptHandlerTooMany() {
    BEGIN_TEST;

    FakeIoApic::Reset();
    fbl::AllocChecker ac;
    auto im = ktl::make_unique<InterruptManager<FakeIoApic>>(&ac);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_EQ(im->Init(), ZX_OK, "");

    uint8_t handler_arg = 0;
    uintptr_t handler = 2;

    static_assert(fbl::count_of(FakeIoApic::entries) >
                  InterruptManager<FakeIoApic>::kNumCpuVectors);

    // Register every interrupt, and store different pointers for them so we
    // can validate it.  All of these should succeed, but will exhaust the
    // allocator.
    for (unsigned i = 0; i < InterruptManager<FakeIoApic>::kNumCpuVectors; ++i) {
        ASSERT_EQ(im->RegisterInterruptHandler(i, reinterpret_cast<int_handler>(handler + i),
                                              &handler_arg + i), ZX_OK, "");
    }

    // Make sure all of the entries are registered
    for (unsigned i = 0; i < InterruptManager<FakeIoApic>::kNumCpuVectors; ++i) {
        uint8_t x86_vector = FakeIoApic::entries[i].x86_vector;
        int_handler installed_handler;
        void* installed_arg;
        im->GetEntryByX86Vector(x86_vector, &installed_handler, &installed_arg);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(installed_handler), handler + i, "");
        EXPECT_EQ(installed_arg, &handler_arg + i, "");
    }

    // Try to allocate one more
    ASSERT_EQ(im->RegisterInterruptHandler(InterruptManager<FakeIoApic>::kNumCpuVectors,
                                          reinterpret_cast<int_handler>(handler), &handler_arg),
              ZX_ERR_NO_RESOURCES, "");

    // Clean up the registered handlers
    for (unsigned i = 0; i < InterruptManager<FakeIoApic>::kNumCpuVectors; ++i) {
        ASSERT_EQ(im->RegisterInterruptHandler(i, nullptr, nullptr), ZX_OK, "");
    }

    END_TEST;
}

UNITTEST_START_TESTCASE(pc_interrupt_tests)
UNITTEST("RegisterInterruptHandler", TestRegisterInterruptHandler)
UNITTEST("UnregisterInterruptHandlerNotRegistered", TestUnregisterInterruptHandlerNotRegistered)
UNITTEST("RegisterInterruptHandlerTooMany", TestRegisterInterruptHandlerTooMany)
UNITTEST_END_TESTCASE(pc_interrupt_tests, "pc_interrupt", "Tests for external interrupts");

} // namespace
