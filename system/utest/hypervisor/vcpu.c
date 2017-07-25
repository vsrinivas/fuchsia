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
    mx_vcpu_io_t vcpu_io;
} test_t;

static mx_status_t vcpu_read_test_state(vcpu_context_t* context, uint32_t kind, void* buffer,
                                        uint32_t len) {
    return MX_ERR_INTERNAL;
}

static mx_status_t vcpu_write_test_state(vcpu_context_t* context, uint32_t kind, const void* buffer,
                                         uint32_t len) {
    if (kind != MX_VCPU_IO || len != sizeof(mx_vcpu_io_t))
        return MX_ERR_INVALID_ARGS;
    test_t* test = (test_t*) context;
    const mx_vcpu_io_t* io = buffer;
    memcpy(&test->vcpu_io, io, sizeof(*io));
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
    test->vcpu.read_state = vcpu_read_test_state;
    test->vcpu.write_state = vcpu_write_test_state;
    return MX_OK;
}

static void tear_down(test_t* test) {
    mtx_destroy(&test->guest_state.mutex);
}

/* Test handling of an IO packet for an input instruction.
 *
 * Expected behavior is to read the value at the provided port address and
 * write the result to RAX.
 */
static bool handle_input_packet(void) {
    BEGIN_TEST;
    test_t test;
    mx_guest_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to initialize test.\n");

    // Initialize the hosts register to an arbitrary non-zero value.
    test.guest_state.io_port_state.uart_line_control = 0xfe;

    // Send a guest packet to to read the UART line control port.
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = true;
    packet.io.port = UART_LINE_CONTROL_IO_PORT;
    packet.io.access_size = 1;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu, &packet), MX_OK, "Failed to handle guest packet.\n");

    // Verify result value was written to RAX.
    EXPECT_EQ(
        test.guest_state.io_port_state.uart_line_control,
        test.vcpu_io.u8,
        "RAX was not populated with expected value.\n");

    END_TEST;
}

/* Test handling of an IO packet for an out instruction.
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
    packet.type = MX_GUEST_PKT_IO;
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

    tear_down(&test);
    END_TEST;
}

/* Test accesses to the PCI config address ports.
 *
 * Access to the 32-bit PCI config address port is provided by the IO ports
 * 0xcf8 - 0xcfb. Accesses to each port must have the same alignment as the
 * port address used.
 *
 * Ex:
 *  -------------------------------------
 * | port  | valid access widths (bytes) |
 * --------------------------------------|
 * | 0xcf8 | 1, 2, 4                     |
 * | 0xcf9 | 1                           |
 * | 0xcfa | 1, 2                        |
 * | 0xcfb | 1                           |
 *  -------------------------------------
 */
static bool write_pci_config_addr_port(void) {
    BEGIN_TEST;

    test_t test;
    mx_guest_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to initialize test.\n");

    // 32 bit write.
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = false;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE;
    packet.io.access_size = 4;
    packet.io.u32 = 0x12345678;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu, &packet), MX_OK, "Failed to handle guest packet.\n");
    EXPECT_EQ(
        0x12345678u,
        test.guest_state.io_port_state.pci_config_address,
        "Incorrect address read from PCI address port.\n");

    // 16 bit write to bits 31..16. Other bits remain unchanged.
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = false;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 2;
    packet.io.access_size = 2;
    packet.io.u16 = 0xFACE;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu, &packet), MX_OK, "Failed to handle guest packet.\n");
    EXPECT_EQ(
        0xFACE5678u,
        test.guest_state.io_port_state.pci_config_address,
        "Incorrect address read from PCI address port.\n");

    // 8 bit write to bits (15..8). Other bits remain unchanged.
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = false;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 1;
    packet.io.access_size = 1;
    packet.io.u8 = 0x99;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu, &packet), MX_OK, "Failed to handle guest packet.\n");
    EXPECT_EQ(
        0xFACE9978u,
        test.guest_state.io_port_state.pci_config_address,
        "Incorrect address read from PCI address port.\n");

    tear_down(&test);
    END_TEST;
}

/* Test reading the PCI config address ports.
 *
 * See write_pci_config_addr_port for more details.
 */
static bool read_pci_config_addr_port(void) {
    BEGIN_TEST;

    test_t test;
    mx_guest_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to initialize test.\n");
    test.guest_state.io_port_state.pci_config_address = 0x12345678;

    // 32 bit read (bits 31..0).
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = true;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE;
    packet.io.access_size = 4;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu, &packet), MX_OK, "Failed to handle guest packet.\n");
    EXPECT_EQ(
        0x12345678u,
        test.vcpu_io.u32,
        "Incorrect address read from PCI address port.\n");

    // 16 bit read (bits 31..16).
    test.vcpu_io.u16 = 0;
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = true;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 2;
    packet.io.access_size = 2;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu, &packet), MX_OK, "Failed to handle guest packet.\n");
    EXPECT_EQ(
        0x1234u,
        test.vcpu_io.u16,
        "Incorrect address read from PCI address port.\n");

    // 8 bit read (bits 15..8).
    test.vcpu_io.u8 = 0;
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = true;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 1;
    packet.io.access_size = 1;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu, &packet), MX_OK, "Failed to handle guest packet.\n");
    EXPECT_EQ(
        0x56u,
        test.vcpu_io.u8,
        "Incorrect address read from PCI address port.\n");

    tear_down(&test);
    END_TEST;
}

BEGIN_TEST_CASE(vcpu)
RUN_TEST(handle_input_packet);
RUN_TEST(handle_output_packet);
RUN_TEST(read_pci_config_addr_port)
RUN_TEST(write_pci_config_addr_port)
END_TEST_CASE(vcpu)
