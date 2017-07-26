// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/pci.h>
#include <hypervisor/pci.h>
#include <unittest/unittest.h>

/* Test we can read multiple fields in 1 32-bit word. */
static bool read_config_register(void) {
    uint32_t value = 0;
    mx_status_t status;
    pci_device_state_t device = {};

    BEGIN_TEST;

    // Access Vendor/Device ID as a single 32bit read.
    status = pci_config_read(&device, /* bus */ 0, PCI_DEVICE_ROOT_COMPLEX,
                             /* func */ 0, PCI_CONFIG_VENDOR_ID, 4, &value);
    EXPECT_EQ(MX_OK, status, "Failed to read VENDOR_ID & DEVICE_ID from PCI config space.\n");
    uint32_t expected_device_vendor = PCI_VENDOR_ID_INTEL | (PCI_DEVICE_ID_INTEL_Q35 << 16);
    EXPECT_EQ(expected_device_vendor, value,
              "Unexpected value read reading Vendor and Device ID together.\n");

    END_TEST;
}

/* Verify we can read portions of a 32 bit word, one byte at a time. */
static bool read_config_register_bytewise(void) {
    uint32_t value = 0;
    mx_status_t status;
    pci_device_state_t device = {};

    BEGIN_TEST;
    uint32_t expected_device_vendor = PCI_VENDOR_ID_INTEL | (PCI_DEVICE_ID_INTEL_Q35 << 16);
    for (int i = 0; i < 4; ++i) {
        status = pci_config_read(&device, /* bus */ 0, PCI_DEVICE_ROOT_COMPLEX,
                                 /* func */ 0, PCI_CONFIG_VENDOR_ID + i, 1,
                                 &value);
        EXPECT_EQ(MX_OK, status, "");
        uint8_t expected_byte = (expected_device_vendor >> (8*i)) & UINT8_MAX;
        EXPECT_EQ(expected_byte, value,
                  "Unexpected value read reading Vendor and Device ID together.\n");


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
    uint32_t value = 0;
    mx_status_t status;
    pci_device_state_t device = {};

    BEGIN_TEST;

    // Set IO enable bit, otherwise reads to the BAR return all 1's.
    device.command = PCI_COMMAND_IO_EN;

    // Set all bits in the BAR register. The device will ignore writes to the
    // LSBs which we can read out to determine the size.
    status = pci_config_write(&device, /* bus */ 0, PCI_DEVICE_ROOT_COMPLEX,
                              /* func */ 0, PCI_CONFIG_BASE_ADDRESSES,
                              4, UINT32_MAX);
    EXPECT_EQ(MX_OK, status, "Failed to write BAR0 to PCI config space.");

    // Read out BAR and compute size.
    status = pci_config_read(&device, /* bus */ 0, PCI_DEVICE_ROOT_COMPLEX,
                             /* func */ 0, PCI_CONFIG_BASE_ADDRESSES,
                             4, &value);
    EXPECT_EQ(MX_OK, status, "Failed to read BAR0 from PCI config space.");
    EXPECT_EQ(
        (uint32_t) PCI_BAR_IO_TYPE_PIO,
        (value & PCI_BAR_IO_TYPE_MASK),
        "Expected PIO bit to be set in BAR.");
    value &= ~PCI_BAR_IO_TYPE_MASK;
    EXPECT_EQ(
        pci_bar_size(PCI_DEVICE_ROOT_COMPLEX),
        (~value) + 1,
        "Incorrect bar size read from pci device.");

    END_TEST;
}

BEGIN_TEST_CASE(pci)
RUN_TEST(read_config_register)
RUN_TEST(read_config_register_bytewise)
RUN_TEST(read_bar_size)
END_TEST_CASE(pci)
