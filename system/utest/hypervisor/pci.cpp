// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/pci.h>
#include <hypervisor/bits.h>
#include <hypervisor/pci.h>
#include <unittest/unittest.h>

/* Test we can read multiple fields in 1 32-bit word. */
static bool read_config_register(void) {
    BEGIN_TEST;

    pci_bus_t bus;
    pci_bus_init(&bus, nullptr);
    pci_device_t* device = &bus.root_complex;

    // Access Vendor/Device ID as a single 32bit read.
    uint32_t value = 0;
    EXPECT_EQ(pci_device_read(device, PCI_CONFIG_VENDOR_ID, 4, &value), MX_OK,
              "Failed to read PCI_CONFIG_VENDOR_ID");
    EXPECT_EQ(value, PCI_VENDOR_ID_INTEL | (PCI_DEVICE_ID_INTEL_Q35 << 16),
              "Unexpected value of PCI_CONFIG_VENDOR_ID");

    END_TEST;
}

/* Verify we can read portions of a 32 bit word, one byte at a time. */
static bool read_config_register_bytewise(void) {
    BEGIN_TEST;

    pci_bus_t bus;
    pci_bus_init(&bus, nullptr);
    pci_device_t* device = &bus.root_complex;

    uint32_t expected_device_vendor = PCI_VENDOR_ID_INTEL | (PCI_DEVICE_ID_INTEL_Q35 << 16);
    for (int i = 0; i < 4; ++i) {
        uint16_t reg = static_cast<uint16_t>(PCI_CONFIG_VENDOR_ID + i);
        uint32_t value = 0;
        EXPECT_EQ(pci_device_read(device, reg, 1, &value), MX_OK,
                  "Failed to read PCI_CONFIG_VENDOR_ID");
        EXPECT_EQ(value, bits_shift(expected_device_vendor, i * 8 + 7, i * 8),
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
static bool read_bar_size(void) {
    BEGIN_TEST;

    pci_bus_t bus;
    pci_bus_init(&bus, nullptr);
    pci_device_t* device = &bus.root_complex;

    // Set all bits in the BAR register. The device will ignore writes to the
    // LSBs which we can read out to determine the size.
    EXPECT_EQ(pci_device_write(device, PCI_CONFIG_BASE_ADDRESSES, 4, UINT32_MAX), MX_OK,
              "Failed to write BAR0 to PCI config space");

    // Read out BAR and compute size.
    uint32_t value = 0;
    EXPECT_EQ(pci_device_read(device, PCI_CONFIG_BASE_ADDRESSES, 4, &value), MX_OK,
              "Failed to read BAR0 from PCI config space");
    EXPECT_EQ(value & PCI_BAR_IO_TYPE_MASK, PCI_BAR_IO_TYPE_PIO,
              "Expected PIO bit to be set in BAR");
    EXPECT_EQ(~(value & ~PCI_BAR_IO_TYPE_MASK) + 1, pci_bar_size(&device->bar[0]),
              "Incorrect bar size read from pci device");

    END_TEST;
}

/* Verify stats & cap registers correctly show present capabilities and that
 * capability data is readable.
 */
static bool read_cap_basic(void) {
    BEGIN_TEST;

    pci_bus_t bus;
    pci_bus_init(&bus, NULL);
    pci_device_t* device = &bus.root_complex;

    // Create and install a simple capability. First two bytes are ignored.
    uint8_t cap_data[] = {0, 0, 0xf, 0xa};
    pci_cap_t cap = {
        .id = 0x9,
        .data = cap_data,
        .len = sizeof(cap_data),
    };
    device->capabilities = &cap;
    device->num_capabilities = 1;

    // PCI Local Bus Spec 3.0 Table 6-2: Status Register Bits
    //
    // This optional read-only bit indicates whether or not this device
    // implements the pointer for a New Capabilities linked list at offset 34h.
    // A value of zero indicates that no New Capabilities linked list is
    // available. A value of one indicates that the value read at offset 34h is
    // a pointer in Configuration Space to a linked list of new capabilities.
    // Refer to Section 6.7 for details on New Capabilities.
    uint32_t status = 0;
    EXPECT_EQ(pci_device_read(device, PCI_CONFIG_STATUS, 2, &status), MX_OK,
              "Failed to read status register from PCI config space.\n");
    EXPECT_TRUE(status & PCI_STATUS_NEW_CAPS,
                "CAP bit not set in status register with a cap list present.\n");

    // Read the cap pointer from config space. Here just verify it points to
    // some location beyond the pre-defined header.
    uint32_t cap_ptr = 0;
    EXPECT_EQ(pci_device_read(device, PCI_CONFIG_CAPABILITIES, 1, &cap_ptr), MX_OK,
              "Failed to read CAP pointer from PCI config space.\n");
    EXPECT_LT(0x40u, cap_ptr, "CAP pointer does not lie beyond the reserved region.\n");

    // Read the capability. This will be the Cap ID, next pointer (0), followed
    // by data bytes (starting at index 2).
    uint32_t cap_value = 0;
    EXPECT_EQ(pci_device_read(device, static_cast<uint16_t>(cap_ptr), 4, &cap_value), MX_OK,
              "Failed to read CAP value from PCI config space.\n");
    EXPECT_EQ(0x0a0f0009u, cap_value,
              "Incorrect CAP value read from PCI config space.\n");

    END_TEST;
}

/* Build a list of capabilities with no data (only the required ID/next
 * fields). Verify the next pointers are correctly wired up to traverse
 * the linked list.
 */
static bool read_cap_chained(void) {
    BEGIN_TEST;

    pci_bus_t bus;
    pci_bus_init(&bus, NULL);
    pci_device_t* device = &bus.root_complex;

    // Build list of caps.
    pci_cap_t caps[5];
    size_t num_caps = sizeof(caps)/sizeof(caps[0]);
    for (uint8_t i = 0; i < num_caps; ++i) {
        caps[i].id = i;
        caps[i].len = 2;
    }
    device->capabilities = caps;
    device->num_capabilities = num_caps;

    uint32_t cap_ptr = 0;
    uint32_t cap_header;
    EXPECT_EQ(pci_device_read(device, PCI_CONFIG_CAPABILITIES, 1, &cap_ptr), MX_OK,
              "Failed to read CAP pointer from PCI config space.\n");
    for (uint8_t i = 0; i < num_caps; ++i) {
        // Read the current capability.
        EXPECT_EQ(pci_device_read(device, static_cast<uint16_t>(cap_ptr), 4, &cap_header), MX_OK,
                  "Failed to read CAP from PCI config space.\n");
        // ID is the first byte.
        EXPECT_EQ(i, cap_header & UINT8_MAX, "Incorrect CAP ID read.\n");
        // Next pointer is the second byte.
        cap_ptr = cap_header >> 8;
    }
    EXPECT_EQ(0u, cap_ptr, "Failed to read CAP pointer from PCI config space.\n");

    END_TEST;
}

BEGIN_TEST_CASE(pci)
RUN_TEST(read_config_register)
RUN_TEST(read_config_register_bytewise)
RUN_TEST(read_bar_size)
RUN_TEST(read_cap_basic)
RUN_TEST(read_cap_chained)
END_TEST_CASE(pci)
