// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/pci.h>

#include <hw/pci.h>
#include <hypervisor/bits.h>
#include <unittest/unittest.h>

#define PCI_CONFIG_ADDRESS_PORT_BASE 0
#define PCI_CONFIG_DATA_PORT_BASE 4

#define PCI_TYPE1_ADDR(bus, device, function, reg)                       \
    (0x80000000 | ((bus) << 16) | ((device) << 11) | ((function) << 8) | \
     ((reg)&PCI_TYPE1_REGISTER_MASK))

/* Test we can read multiple fields in 1 32-bit word. */
static bool pci_device_read_config_register(void) {
    BEGIN_TEST;

    Guest guest;
    PciBus bus(&guest, nullptr);
    bus.Init();
    PciDevice& device = bus.root_complex();

    // Access Vendor/Device ID as a single 32bit read.
    IoValue value = {};
    value.access_size = 4;
    EXPECT_EQ(device.ReadConfig(PCI_CONFIG_VENDOR_ID, &value), ZX_OK,
              "Failed to read PCI_CONFIG_VENDOR_ID");
    EXPECT_EQ(value.u32, PCI_VENDOR_ID_INTEL | (PCI_DEVICE_ID_INTEL_Q35 << 16),
              "Unexpected value of PCI_CONFIG_VENDOR_ID");

    END_TEST;
}

/* Verify we can read portions of a 32 bit word, one byte at a time. */
static bool pci_device_read_config_register_bytewise(void) {
    BEGIN_TEST;

    Guest guest;
    PciBus bus(&guest, nullptr);
    bus.Init();
    PciDevice& device = bus.root_complex();

    uint32_t expected_device_vendor = PCI_VENDOR_ID_INTEL | (PCI_DEVICE_ID_INTEL_Q35 << 16);
    for (int i = 0; i < 4; ++i) {
        uint16_t reg = static_cast<uint16_t>(PCI_CONFIG_VENDOR_ID + i);
        IoValue value = {};
        value.access_size = 1;
        EXPECT_EQ(device.ReadConfig(reg, &value), ZX_OK,
                  "Failed to read PCI_CONFIG_VENDOR_ID");
        EXPECT_EQ(value.u32, bits_shift(expected_device_vendor, i * 8 + 7, i * 8),
                  "Unexpected value of PCI_CONFIG_VENDOR_ID");
    }

    END_TEST;
}

/* PCI devices BAR sizes must be a power of 2 and must not support setting any
 * bits in the BAR that are not size aligned. Software often relies on this to
 * read the bar size by writing all 1's to the register and reading back the
 * value.
 *
 * This tests that we properly mask the lowest bits so software can compute the
 * BAR size.
 */
static bool pci_device_read_bar_size(void) {
    BEGIN_TEST;

    Guest guest;
    PciBus bus(&guest, nullptr);
    bus.Init();
    PciDevice& device = bus.root_complex();

    // Set all bits in the BAR register. The device will ignore writes to the
    // LSBs which we can read out to determine the size.
    IoValue value;
    value.access_size = 4;
    value.u32 = UINT32_MAX;
    EXPECT_EQ(device.WriteConfig(PCI_CONFIG_BASE_ADDRESSES, value), ZX_OK,
              "Failed to write BAR0 to PCI config space");

    // Read out BAR and compute size.
    value.access_size = 4;
    value.u32 = 0;
    EXPECT_EQ(device.ReadConfig(PCI_CONFIG_BASE_ADDRESSES, &value), ZX_OK,
              "Failed to read BAR0 from PCI config space");
    EXPECT_EQ(value.u32 & PCI_BAR_ASPACE_MASK, PCI_BAR_ASPACE_MMIO,
              "Expected PIO bit to be set in BAR");
    const PciBar* bar = device.bar(0);
    ASSERT_NOT_NULL(bar);
    EXPECT_EQ(~(value.u32 & ~PCI_BAR_ASPACE_MASK) + 1, bar->size,
              "Incorrect bar size read from pci device");

    END_TEST;
}

/* Verify stats & cap registers correctly show present capabilities and that
 * capability data is readable.
 */
static bool pci_device_read_cap_basic(void) {
    BEGIN_TEST;

    Guest guest;
    PciBus bus(&guest, nullptr);
    bus.Init();
    PciDevice& device = bus.root_complex();

    // Create and install a simple capability. First two bytes are ignored.
    uint8_t cap_data[] = {0, 0, 0xf, 0xa};
    pci_cap_t cap = {
        .id = 0x9,
        .data = cap_data,
        .len = sizeof(cap_data),
    };
    device.set_capabilities(&cap, 1);

    // PCI Local Bus Spec 3.0 Table 6-2: Status Register Bits
    //
    // This optional read-only bit indicates whether or not this device
    // implements the pointer for a New Capabilities linked list at offset 34h.
    // A value of zero indicates that no New Capabilities linked list is
    // available. A value of one indicates that the value read at offset 34h is
    // a pointer in Configuration Space to a linked list of new capabilities.
    // Refer to Section 6.7 for details on New Capabilities.
    IoValue status;
    status.access_size = 2;
    status.u16 = 0;
    EXPECT_EQ(device.ReadConfig(PCI_CONFIG_STATUS, &status), ZX_OK,
              "Failed to read status register from PCI config space.\n");
    EXPECT_TRUE(status.u16 & PCI_STATUS_NEW_CAPS,
                "CAP bit not set in status register with a cap list present.\n");

    // Read the cap pointer from config space. Here just verify it points to
    // some location beyond the pre-defined header.
    IoValue cap_ptr;
    cap_ptr.access_size = 1;
    cap_ptr.u8 = 0;
    EXPECT_EQ(device.ReadConfig(PCI_CONFIG_CAPABILITIES, &cap_ptr), ZX_OK,
              "Failed to read CAP pointer from PCI config space.\n");
    EXPECT_LT(0x40u, cap_ptr.u8, "CAP pointer does not lie beyond the reserved region.\n");

    // Read the capability. This will be the Cap ID, next pointer (0), followed
    // by data bytes (starting at index 2).
    IoValue cap_value;
    cap_value.access_size = 4;
    cap_value.u32 = 0;
    EXPECT_EQ(device.ReadConfig(cap_ptr.u8, &cap_value), ZX_OK,
              "Failed to read CAP value from PCI config space.\n");
    EXPECT_EQ(0x0a0f0009u, cap_value.u32,
              "Incorrect CAP value read from PCI config space.\n");

    END_TEST;
}

/* Build a list of capabilities with no data (only the required ID/next
 * fields). Verify the next pointers are correctly wired up to traverse
 * the linked list.
 */
static bool pci_device_read_cap_chained(void) {
    BEGIN_TEST;

    Guest guest;
    PciBus bus(&guest, nullptr);
    bus.Init();
    PciDevice& device = bus.root_complex();

    // Build list of caps.
    pci_cap_t caps[5];
    size_t num_caps = sizeof(caps) / sizeof(caps[0]);
    for (uint8_t i = 0; i < num_caps; ++i) {
        caps[i].id = i;
        caps[i].len = 2;
    }
    device.set_capabilities(caps, num_caps);

    IoValue cap_ptr;
    cap_ptr.access_size = 1;
    cap_ptr.u8 = 0;
    EXPECT_EQ(device.ReadConfig(PCI_CONFIG_CAPABILITIES, &cap_ptr), ZX_OK,
              "Failed to read CAP pointer from PCI config space.\n");
    for (uint8_t i = 0; i < num_caps; ++i) {
        IoValue cap_header;
        cap_header.access_size = 4;
        cap_header.u32 = 0;

        // Read the current capability.
        EXPECT_EQ(device.ReadConfig(cap_ptr.u8, &cap_header), ZX_OK,
                  "Failed to read CAP from PCI config space.\n");
        // ID is the first byte.
        EXPECT_EQ(i, cap_header.u32 & UINT8_MAX, "Incorrect CAP ID read.\n");
        // Next pointer is the second byte.
        cap_ptr.u8 = static_cast<uint8_t>(cap_header.u32 >> 8);
    }
    EXPECT_EQ(0u, cap_ptr.u8, "Failed to read CAP pointer from PCI config space.\n");

    END_TEST;
}

/* Test accesses to the PCI config address ports.
 *
 * Access to the 32-bit PCI config address port is provided by the IO ports
 * 0xcf8 - 0xcfb. Accesses to each port must have the same alignment as the
 * port address used.
 *
 * The device operates on relative port addresses so we'll use 0-3 instead of
 * 0cf8-0xcfb
 *
 * Ex:
 *  -------------------------------------
 * | port  | valid access widths (bytes) |
 * --------------------------------------|
 * |   0   | 1, 2, 4                     |
 * |   1   | 1                           |
 * |   2   | 1, 2                        |
 * |   3   | 1                           |
 *  -------------------------------------
 */
static bool pci_bus_write_config_addr_port(void) {
    BEGIN_TEST;

    Guest guest;
    PciBus bus(&guest, nullptr);
    bus.Init();

    // 32 bit write.
    IoValue value;
    value.access_size = 4;
    value.u32 = 0x12345678;
    EXPECT_EQ(bus.WriteIoPort(PCI_CONFIG_ADDRESS_PORT_BASE, value), ZX_OK);
    EXPECT_EQ(bus.config_addr(), 0x12345678u);

    // 16 bit write to bits 31..16. Other bits remain unchanged.
    value.access_size = 2;
    value.u16 = 0xFACE;
    EXPECT_EQ(bus.WriteIoPort(PCI_CONFIG_ADDRESS_PORT_BASE + 2, value), ZX_OK);
    EXPECT_EQ(bus.config_addr(), 0xFACE5678u);

    // 8 bit write to bits (15..8). Other bits remain unchanged.
    value.access_size = 1;
    value.u8 = 0x99;
    EXPECT_EQ(bus.WriteIoPort(PCI_CONFIG_ADDRESS_PORT_BASE + 1, value), ZX_OK);
    EXPECT_EQ(bus.config_addr(), 0xFACE9978u);

    END_TEST;
}

/* Test reading the PCI config address ports.
 *
 * See pci_bus_write_config_addr_port for more details.
 */
static bool pci_bus_read_config_addr_port(void) {
    BEGIN_TEST;

    Guest guest;
    PciBus bus(&guest, nullptr);
    bus.Init();
    bus.set_config_addr(0x12345678);

    // 32 bit read (bits 31..0).
    IoValue value = {};
    value.access_size = 4;
    EXPECT_EQ(bus.ReadIoPort(PCI_CONFIG_ADDRESS_PORT_BASE, &value), ZX_OK);
    EXPECT_EQ(value.access_size, 4, "Incorrect IO access_size");
    EXPECT_EQ(value.u32, 0x12345678u, "Incorrect address read from PCI address port");

    // 16 bit read (bits 31..16).
    value.access_size = 2;
    value.u16 = 0;
    EXPECT_EQ(bus.ReadIoPort(PCI_CONFIG_ADDRESS_PORT_BASE + 2, &value), ZX_OK);
    EXPECT_EQ(value.access_size, 2, "Incorrect IO access_size");
    EXPECT_EQ(value.u16, 0x1234u, "Incorrect address read from PCI address port");

    // 8 bit read (bits 15..8).
    value.access_size = 1;
    value.u8 = 0;
    EXPECT_EQ(bus.ReadIoPort(PCI_CONFIG_ADDRESS_PORT_BASE + 1, &value), ZX_OK);
    EXPECT_EQ(value.access_size, 1, "Incorrect IO access_size");
    EXPECT_EQ(value.u8, 0x56u, "Incorrect address read from PCI address port");

    END_TEST;
}

/* The address written to the data port (0xcf8) is 4b aligned. The offset into
 * the data port range 0xcfc-0xcff is added to the address to access partial
 * words.
 */
static bool pci_bus_read_config_data_port(void) {
    BEGIN_TEST;

    Guest guest;
    PciBus bus(&guest, nullptr);
    bus.Init();
    IoValue value = {};

    // 16-bit read.
    bus.set_config_addr(PCI_TYPE1_ADDR(0, 0, 0, 0));
    value.access_size = 2;
    EXPECT_EQ(bus.ReadIoPort(PCI_CONFIG_DATA_PORT_BASE, &value), ZX_OK);
    EXPECT_EQ(value.access_size, 2, "Incorrect IO access_size");
    EXPECT_EQ(value.u16, PCI_VENDOR_ID_INTEL, "Incorrect value read from PCI data port");

    // 32-bit read from same address. Result should now contain the Device ID
    // in the upper 16 bits
    value.access_size = 4;
    value.u32 = 0;
    EXPECT_EQ(bus.ReadIoPort(PCI_CONFIG_DATA_PORT_BASE, &value), ZX_OK);
    EXPECT_EQ(value.access_size, 4, "Incorrect IO access_size");
    EXPECT_EQ(value.u32, PCI_VENDOR_ID_INTEL | (PCI_DEVICE_ID_INTEL_Q35 << 16),
              "Incorrect value read from PCI data port");

    // 16-bit read of upper half-word.
    //
    // Device ID is 2b aligned and the PCI config address register can only hold
    // a 4b aligned address. The offset into the word addressed by the PCI
    // address port is added to the data port address.
    value.access_size = 2;
    value.u16 = 0;
    bus.set_config_addr(PCI_TYPE1_ADDR(0, 0, 0, PCI_CONFIG_DEVICE_ID));
    // Verify we're using a 4b aligned register address.
    EXPECT_EQ(bus.config_addr() & bit_mask<uint32_t>(2), 0u);
    // Add the register offset to the data port base address.
    EXPECT_EQ(
        bus.ReadIoPort(
            PCI_CONFIG_DATA_PORT_BASE + (PCI_CONFIG_DEVICE_ID & bit_mask<uint32_t>(2)),
            &value),
        ZX_OK);
    EXPECT_EQ(value.access_size, 2, "Incorrect IO access_size");
    EXPECT_EQ(value.u16, PCI_DEVICE_ID_INTEL_Q35, "Incorrect value read from PCI data port");

    END_TEST;
}

BEGIN_TEST_CASE(pci)
RUN_TEST(pci_device_read_config_register)
RUN_TEST(pci_device_read_config_register_bytewise)
RUN_TEST(pci_device_read_bar_size)
RUN_TEST(pci_device_read_cap_basic)
RUN_TEST(pci_device_read_cap_chained)
RUN_TEST(pci_bus_write_config_addr_port)
RUN_TEST(pci_bus_read_config_addr_port)
RUN_TEST(pci_bus_read_config_data_port)
END_TEST_CASE(pci)
