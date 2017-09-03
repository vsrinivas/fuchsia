// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/pci.h>
#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/guest.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/io_port.h>
#include <hypervisor/pci.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <unittest/unittest.h>

#define PCI_TYPE1_ADDR(bus, device, function, reg)                                                 \
    (0x80000000 | ((bus) << 16) | ((device) << 11) | ((function) << 8) |                           \
     ((reg)&PCI_TYPE1_REGISTER_MASK))

typedef struct test {
    vcpu_ctx_t vcpu_ctx;
    guest_ctx_t guest_ctx;
    io_apic_t io_apic;
    io_port_t io_port;
    pci_bus_t bus;
    mx_vcpu_io_t vcpu_io;
} test_t;

static mx_status_t vcpu_read_test_state(vcpu_ctx_t* vcpu_ctx, uint32_t kind, void* buffer,
                                        uint32_t len) {
    return MX_ERR_INTERNAL;
}

static mx_status_t vcpu_write_test_state(vcpu_ctx_t* vcpu_ctx, uint32_t kind, const void* buffer,
                                         uint32_t len) {
    if (kind != MX_VCPU_IO || len != sizeof(mx_vcpu_io_t))
        return MX_ERR_INVALID_ARGS;
    test_t* test = (test_t*)vcpu_ctx;
    auto io = static_cast<const mx_vcpu_io_t*>(buffer);
    memcpy(&test->vcpu_io, io, sizeof(*io));
    return MX_OK;
}

static mx_status_t setup(test_t* test) {
    memset(test, 0, sizeof(*test));
    vcpu_init(&test->vcpu_ctx);
    io_apic_init(&test->io_apic);
    io_port_init(&test->io_port);
    pci_bus_init(&test->bus, &test->io_apic);

    test->guest_ctx.io_apic = &test->io_apic;
    test->guest_ctx.io_port = &test->io_port;
    test->guest_ctx.bus = &test->bus;
    test->vcpu_ctx.guest_ctx = &test->guest_ctx;

    // Redirect read/writes to the VCPU state to just access a field in the
    // test structure.
    test->vcpu_ctx.read_state = vcpu_read_test_state;
    test->vcpu_ctx.write_state = vcpu_write_test_state;
    return MX_OK;
}

/* Test handling of an IO packet for an input instruction.
 *
 * Expected behavior is to read the value at the provided port address and
 * write the result to RAX.
 */
static bool handle_input_packet(void) {
    BEGIN_TEST;

    test_t test;
    mx_port_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to initialize test");

    // Initialize the hosts register to an arbitrary non-zero value.
    uart_t uart;
    uart_init(&uart, &test.io_apic);
    uart.line_control = 0xfe;
    test.guest_ctx.uart = &uart;

    // Send a guest packet to to read the UART line control port.
    packet.type = MX_PKT_TYPE_GUEST_IO;
    packet.guest_io.input = true;
    packet.guest_io.port = UART_LINE_CONTROL_PORT;
    packet.guest_io.access_size = 1;
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");

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
    mx_packet_guest_io_t io = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to initialize test");

    uart_t uart;
    uart_init(&uart, &test.io_apic);
    test.guest_ctx.uart = &uart;

    // Send a guest packet to to write the UART line control port.
    io.input = false;
    io.port = UART_LINE_CONTROL_PORT;
    io.access_size = 1;
    io.u8 = 0xaf;
    EXPECT_EQ(uart_write(&uart, &io), MX_OK, "Failed to handle UART IO packet");

    // Verify packet value was saved to the host port state.
    EXPECT_EQ(io.u8, uart.line_control, "UART was not populated with expected value");

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
    mx_port_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to setup test");

    // 32 bit write.
    packet.type = MX_PKT_TYPE_GUEST_IO;
    packet.guest_io.input = false;
    packet.guest_io.port = PCI_CONFIG_ADDRESS_PORT_BASE;
    packet.guest_io.access_size = 4;
    packet.guest_io.u32 = 0x12345678;
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");
    EXPECT_EQ(test.bus.config_addr, 0x12345678u, "Incorrect address read from PCI address port");

    // 16 bit write to bits 31..16. Other bits remain unchanged.
    packet.type = MX_PKT_TYPE_GUEST_IO;
    packet.guest_io.input = false;
    packet.guest_io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 2;
    packet.guest_io.access_size = 2;
    packet.guest_io.u16 = 0xFACE;
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");
    EXPECT_EQ(test.bus.config_addr, 0xFACE5678u, "Incorrect address read from PCI address port");

    // 8 bit write to bits (15..8). Other bits remain unchanged.
    packet.type = MX_PKT_TYPE_GUEST_IO;
    packet.guest_io.input = false;
    packet.guest_io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 1;
    packet.guest_io.access_size = 1;
    packet.guest_io.u8 = 0x99;
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");
    EXPECT_EQ(test.bus.config_addr, 0xFACE9978u, "Incorrect address read from PCI address port");

    END_TEST;
}

/* Test reading the PCI config address ports.
 *
 * See write_pci_config_addr_port for more details.
 */
static bool read_pci_config_addr_port(void) {
    BEGIN_TEST;

    test_t test;
    mx_port_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to setup test");
    test.bus.config_addr = 0x12345678;

    // 32 bit read (bits 31..0).
    packet.type = MX_PKT_TYPE_GUEST_IO;
    packet.guest_io.input = true;
    packet.guest_io.port = PCI_CONFIG_ADDRESS_PORT_BASE;
    packet.guest_io.access_size = 4;
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 4, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u32, 0x12345678u, "Incorrect address read from PCI address port");

    // 16 bit read (bits 31..16).
    test.vcpu_io.u16 = 0;
    packet.type = MX_PKT_TYPE_GUEST_IO;
    packet.guest_io.input = true;
    packet.guest_io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 2;
    packet.guest_io.access_size = 2;
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 2, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u16, 0x1234u, "Incorrect address read from PCI address port");

    // 8 bit read (bits 15..8).
    test.vcpu_io.u8 = 0;
    packet.type = MX_PKT_TYPE_GUEST_IO;
    packet.guest_io.input = true;
    packet.guest_io.port = PCI_CONFIG_ADDRESS_PORT_BASE + 1;
    packet.guest_io.access_size = 1;
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 1, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u8, 0x56u, "Incorrect address read from PCI address port");

    END_TEST;
}

/* The address written to the data port (0xcf8) is 4b aligned. The offset into
 * the data port range 0xcfc-0xcff is added to the address to access partial
 * words.
 */
static bool read_pci_config_data_port(void) {
    BEGIN_TEST;

    test_t test;
    mx_port_packet_t packet = {};
    ASSERT_EQ(setup(&test), MX_OK, "Failed to setup test");

    // 16-bit read.
    test.bus.config_addr = PCI_TYPE1_ADDR(0, 0, 0, 0);
    packet.type = MX_PKT_TYPE_GUEST_IO;
    packet.guest_io.input = true;
    packet.guest_io.port = PCI_CONFIG_DATA_PORT_BASE;
    packet.guest_io.access_size = 2;
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 2, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u16, PCI_VENDOR_ID_INTEL, "Incorrect value read from PCI data port");

    // 32-bit read from same address. Result should now contain the Device ID
    // in the upper 16 bits
    packet.guest_io.access_size = 4;
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 4, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u32, PCI_VENDOR_ID_INTEL | (PCI_DEVICE_ID_INTEL_Q35 << 16),
              "Incorrect value read from PCI data port");

    // 16-bit read of upper half-word.
    //
    // Device ID is 2b aligned and the PCI config address register can only hold
    // a 4b aligned address. The offset into the word addressed by the PCI
    // address port is added to the data port address.
    test.vcpu_io.u32 = 0;
    packet.guest_io.access_size = 2;
    test.bus.config_addr = PCI_TYPE1_ADDR(0, 0, 0, PCI_CONFIG_DEVICE_ID);
    // Verify we're using a 4b aligned register address.
    EXPECT_EQ(test.bus.config_addr & bit_mask<uint32_t>(2), 0u);
    // Add the register offset to the data port base address.
    packet.guest_io.port =
        PCI_CONFIG_DATA_PORT_BASE + (PCI_CONFIG_DEVICE_ID & bit_mask<uint32_t>(2));
    EXPECT_EQ(vcpu_packet_handler(&test.vcpu_ctx, &packet), MX_OK, "Failed to handle guest packet");
    EXPECT_EQ(test.vcpu_io.access_size, 2, "Incorrect IO access_size");
    EXPECT_EQ(test.vcpu_io.u16, PCI_DEVICE_ID_INTEL_Q35, "Incorrect value read from PCI data port");

    END_TEST;
}

BEGIN_TEST_CASE(vcpu)
RUN_TEST(handle_input_packet);
RUN_TEST(handle_output_packet);
RUN_TEST(read_pci_config_addr_port)
RUN_TEST(write_pci_config_addr_port)
RUN_TEST(read_pci_config_data_port)
END_TEST_CASE(vcpu)
