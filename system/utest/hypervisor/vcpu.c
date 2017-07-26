// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/guest.h>
#include <hypervisor/ports.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/types.h>
#include <unittest/unittest.h>

typedef struct test {
    vcpu_context_t vcpu;
    guest_state_t guest_state;
    mx_vcpu_state_t vcpu_state;
} test_t;

static mx_status_t vcpu_write_test_state(vcpu_context_t* vcpu_context, mx_vcpu_state_t* state) {
    test_t* test = (test_t*) vcpu_context;
    memcpy(&test->vcpu_state, state, sizeof(*state));
    return MX_OK;
}

static mx_status_t vcpu_read_test_state(vcpu_context_t* vcpu_context, mx_vcpu_state_t* state) {
    test_t* test = (test_t*) vcpu_context;
    memcpy(state, &test->vcpu_state, sizeof(*state));
    return MX_OK;
}

static mx_status_t setup(test_t* test) {
    memset(test, 0, sizeof(*test));
    vcpu_init(&test->vcpu);

    int ret = mtx_init(&test->guest_state.mutex, mtx_plain);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to initialize guest state mutex\n");
        return MX_ERR_INTERNAL;
    }

    test->vcpu.guest_state = &test->guest_state;

    // Redirect read/writes to the VCPU state to just access a field in the
    // test structure.
    test->vcpu.read_state = &vcpu_read_test_state;
    test->vcpu.write_state = &vcpu_write_test_state;
    return MX_OK;
}

static void tear_down(test_t* test) {
    mtx_destroy(&test->guest_state.mutex);
}

/* Test handling of an io packet for an input instruction.
 *
 * Expected behavior is to read the value at the provided port address and
 * write the result to rax.
 */
static bool handle_input_packet(void) {
    BEGIN_TEST;
    test_t test;
    mx_guest_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to initialize test.\n");

    // Initialize the hosts register to an abitrary non-zero value.
    test.guest_state.io_port_state.uart_line_control = 0xfe;

    // Send a guest packet to to read the UART line control port.
    packet.type = MX_GUEST_PKT_TYPE_IO;
    packet.io.input = true;
    packet.io.port = UART_LINE_CONTROL_IO_PORT;
    packet.io.access_size = 1;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu, &packet), MX_OK, "Failed to handle guest packet.\n");

    // Verify result value was written to rax.
    EXPECT_EQ(
        test.guest_state.io_port_state.uart_line_control,
        test.vcpu_state.rax,
        "RAX was not populated with expected value.\n");

    END_TEST;
}

/* Test handling of an io packet for an out instruction.
 *
 * Expected behavior is for the value to be saved into a host data structure.
 */
static bool handle_output_packet(void) {
    BEGIN_TEST;
    test_t test;
    mx_guest_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to initialize test.\n");
    test.guest_state.io_port_state.uart_line_control = 0;

    // Send a guest packet to to write the UART line control port.
    packet.type = MX_GUEST_PKT_TYPE_IO;
    packet.io.input = false;
    packet.io.port = UART_LINE_CONTROL_IO_PORT;
    packet.io.access_size = 1;
    packet.io.u8 = 0xaf;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu, &packet), MX_OK, "Failed to handle guest packet.\n");

    // Verify packet value was saved to the host port state.
    EXPECT_EQ(
        packet.io.u8,
        test.guest_state.io_port_state.uart_line_control,
        "io_port_state was not populated with expected value.\n");

    END_TEST;
}

BEGIN_TEST_CASE(vcpu)
RUN_TEST(handle_input_packet);
RUN_TEST(handle_output_packet);
END_TEST_CASE(vcpu)
