// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if ARCH_X86
#include <arch/x86/apic.h>
#endif
#include <lib/unittest/unittest.h>
#include <object/interrupt_dispatcher.h>
#include <object/interrupt_event_dispatcher.h>
#include <object/port_dispatcher.h>
#include <platform.h>

namespace {

// Tests that if an irq handler fires at the same time as an interrupt dispatcher is destroyed
// the system does not deadlock.
static bool TestConcurrentIntEventDispatcherTeardown() {
    BEGIN_TEST;

    // Generating the interrupt events for this test is necessarily arch specific and is only
    // implemented for x86 here.
#if ARCH_X86
    KernelHandle<InterruptDispatcher> interrupt_handle;
    zx_rights_t rights;

    uint32_t gsi;
    constexpr uint32_t gsi_search_max = 24;
    for (gsi = 0; gsi < gsi_search_max; gsi++) {
        zx_status_t status = InterruptEventDispatcher::Create(&interrupt_handle, &rights, gsi,
                                                              ZX_INTERRUPT_MODE_DEFAULT);
        if (status == ZX_OK) {
            break;
        }
    }
    ASSERT_NE(gsi, gsi_search_max, "Failed to find free global interrupt");

    fbl::RefPtr<InterruptDispatcher> interrupt = interrupt_handle.release();

    // Find the local vector
    uint8_t vector = apic_io_fetch_irq_vector(gsi);

    // Spin up a thread to generate the interrupt. As IPIs cannot be masked this causes the
    // associated InterruptDispatcher handler to constantly get invoked, which is what we want.
    thread_t* int_thread = thread_create(
        "int",
        [](void* arg) -> int {
            uint8_t* vec = static_cast<uint8_t*>(arg);
            while (1) {
                apic_send_self_ipi(*vec, DELIVERY_MODE_FIXED);
                thread_yield();
                thread_process_pending_signals();
            }
            return -1;
        },
        &vector, DEFAULT_PRIORITY);
    thread_resume(int_thread);

    // Remove the interrupt and if we don't deadlock and keep executing then all is well.
    interrupt.reset();

    // Shutdown the test.
    thread_kill(int_thread);
    zx_status_t status = thread_join(int_thread, nullptr, current_time() + ZX_SEC(5));
    EXPECT_EQ(status, ZX_OK, "");
#endif

    END_TEST;
}

} // namespace

UNITTEST_START_TESTCASE(interrupt_event_dispatcher_tests)
UNITTEST("ConcurrentIntEventDispatcherTeardown", TestConcurrentIntEventDispatcherTeardown)
UNITTEST_END_TESTCASE(interrupt_event_dispatcher_tests, "interrupt_event_dispatcher_tests",
                      "InterruptEventDispatcher tests");
