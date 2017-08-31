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
        EXPECT_EQ(value, BITS_SHIFT(expected_device_vendor, i * 8 + 7, i * 8),
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
    EXPECT_EQ(
        pci_device_write(device, PCI_CONFIG_BASE_ADDRESSES, 4, UINT32_MAX), MX_OK,
        "Failed to write BAR0 to PCI config space");

    // Read out BAR and compute size.
    uint32_t value = 0;
    EXPECT_EQ(pci_device_read(device, PCI_CONFIG_BASE_ADDRESSES, 4, &value), MX_OK,
              "Failed to read BAR0 from PCI config space");
    EXPECT_EQ(value & PCI_BAR_IO_TYPE_MASK, PCI_BAR_IO_TYPE_PIO,
              "Expected PIO bit to be set in BAR");
    EXPECT_EQ(~(value & ~PCI_BAR_IO_TYPE_MASK) + 1, pci_bar_size(device),
              "Incorrect bar size read from pci device");

    END_TEST;
}

BEGIN_TEST_CASE(pci)
RUN_TEST(read_config_register)
RUN_TEST(read_config_register_bytewise)
RUN_TEST(read_bar_size)
END_TEST_CASE(pci)
