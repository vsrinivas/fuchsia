// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/pci.h>
#include <hypervisor/bits.h>
#include <hypervisor/guest.h>
#include <hypervisor/pci.h>
#include <hypervisor/ports.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/types.h>
#include <unittest/unittest.h>

#define PCI_TYPE1_ADDR(bus, device, function, reg) \
    (0x80000000 | ((bus) << 16) | ((device) << 11) | ((function) << 8) | \
        ((reg) & PCI_TYPE1_REGISTER_MASK))

typedef struct test {
    vcpu_context_t vcpu_context;
    guest_state_t guest_state;
    pci_bus_t bus;
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
    vcpu_init(&test->vcpu_context);
    pci_bus_init(test->bus);

    test->guest_state.bus = test->bus;
    test->vcpu_context.guest_state = &test->guest_state;

    // Redirect read/writes to the VCPU state to just access a field in the
    // test structure.
    test->vcpu_context.read_state = vcpu_read_test_state;
    test->vcpu_context.write_state = vcpu_write_test_state;
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
    ASSERT_EQ(setup(&test), MX_OK, "Failed to initialize test");

    // Initialize the hosts register to an arbitrary non-zero value.
    uart_t uart;
    uart_init(&uart);
    uart.line_control = 0xfe;
    test.guest_state.uart = &uart;

    // Send a guest packet to to read the UART line control port.
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = true;
    packet.io.port = UART_LINE_CONTROL_PORT;
    packet.io.access_size = 1;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");

    // Verify result value was written to RAX.
    EXPECT_EQ(test.vcpu_io.u8, uart.line_control, "RAX was not populated with expected value");

    END_TEST;
}

/* Test handling of an IO packet for an out instruction.
 *
 * Expected behavior is for the value to be saved into a host data structure.
 */
static bool handle_output_packet(void) {
    BEGIN_TEST;

    test_t test;
    mx_guest_io_t io = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to initialize test");

    uart_t uart;
    uart_init(&uart);
    test.guest_state.uart = &uart;

    // Send a guest packet to to write the UART line control port.
    io.input = false;
    io.port = UART_LINE_CONTROL_PORT;
    io.access_size = 1;
    io.u8 = 0xaf;
    EXPECT_EQ(uart_write(&test.guest_state, 0, &io), MX_OK,
              "Failed to handle UART IO packet");

    // Verify packet value was saved to the host port state.
    EXPECT_EQ(io.u8, uart.line_control, "UART was not populated with expected value");

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
    ASSERT_EQ(setup(&test), MX_OK, "Failed to setup test");

    // 32 bit write.
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = false;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE;
    packet.io.access_size = 4;
    packet.io.u32 = 0x12345678;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");
    EXPECT_EQ(test.guest_state.io_port_state.pci_config_address, 0x12345678u,
              "Incorrect address read from PCI address port");

    // 16 bit write to bits 31..16. Other bits remain unchanged.
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = false;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 2;
    packet.io.access_size = 2;
    packet.io.u16 = 0xFACE;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");
    EXPECT_EQ(test.guest_state.io_port_state.pci_config_address, 0xFACE5678u,
              "Incorrect address read from PCI address port");

    // 8 bit write to bits (15..8). Other bits remain unchanged.
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = false;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 1;
    packet.io.access_size = 1;
    packet.io.u8 = 0x99;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");
    EXPECT_EQ(test.guest_state.io_port_state.pci_config_address, 0xFACE9978u,
              "Incorrect address read from PCI address port");

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
    ASSERT_EQ(setup(&test), MX_OK, "Failed to setup test");
    test.guest_state.io_port_state.pci_config_address = 0x12345678;

    // 32 bit read (bits 31..0).
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = true;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE;
    packet.io.access_size = 4;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 4, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u32, 0x12345678u, "Incorrect address read from PCI address port");

    // 16 bit read (bits 31..16).
    test.vcpu_io.u16 = 0;
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = true;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 2;
    packet.io.access_size = 2;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 2, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u16, 0x1234u, "Incorrect address read from PCI address port");

    // 8 bit read (bits 15..8).
    test.vcpu_io.u8 = 0;
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = true;
    packet.io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 1;
    packet.io.access_size = 1;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 1, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u8, 0x56u, "Incorrect address read from PCI address port");

    tear_down(&test);

    END_TEST;
}

/* The address written to the data port (0xcf8) is 4b aligned. The offset into
 * the data port range 0xcfc-0xcff is added to the address to access partial
 * words.
 */
static bool read_pci_config_data_port(void) {
    BEGIN_TEST;

    test_t test;
    mx_guest_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to setup test");

    // 16-bit read.
    test.guest_state.io_port_state.pci_config_address = PCI_TYPE1_ADDR(0, 0, 0, 0);
    packet.type = MX_GUEST_PKT_IO;
    packet.io.input = true;
    packet.io.port = PCI_CONFIG_DATA_PORT_BASE;
    packet.io.access_size = 2;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 2, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u16, PCI_VENDOR_ID_INTEL, "Incorrect value read from PCI data port");

    // 32-bit read from same address. Result should now contain the Device ID
    // in the upper 16 bits
    packet.io.access_size = 4;
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 4, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u32, PCI_VENDOR_ID_INTEL | (PCI_DEVICE_ID_INTEL_Q35 << 16),
              "Incorrect value read from PCI data port");

    // 16-bit read of upper half-word.
    //
    // Device ID is 2b aligned and the PCI config address register can only hold
    // a 4b aligned address. The offset into the word addressed by the PCI
    // address port is added to the data port address.
    test.vcpu_io.u32 = 0;
    packet.io.access_size = 2;
    test.guest_state.io_port_state.pci_config_address = PCI_TYPE1_ADDR(0, 0, 0,
                                                                       PCI_CONFIG_DEVICE_ID);
    // Verify we're using a 4b aligned register address.
    EXPECT_EQ(test.guest_state.io_port_state.pci_config_address & BIT_MASK(2), 0u, "");
    // Add the register offset to the data port base address.
    packet.io.port = PCI_CONFIG_DATA_PORT_BASE + (PCI_CONFIG_DEVICE_ID & BIT_MASK(2));
    EXPECT_EQ(vcpu_handle_packet(&test.vcpu_context, &packet), MX_OK,
              "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 2, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u16, PCI_DEVICE_ID_INTEL_Q35,
              "Incorrect value read from PCI data port");

    tear_down(&test);

    END_TEST;
}

BEGIN_TEST_CASE(vcpu)
RUN_TEST(handle_input_packet);
RUN_TEST(handle_output_packet);
RUN_TEST(read_pci_config_addr_port)
RUN_TEST(write_pci_config_addr_port)
RUN_TEST(read_pci_config_data_port)
END_TEST_CASE(vcpu)
