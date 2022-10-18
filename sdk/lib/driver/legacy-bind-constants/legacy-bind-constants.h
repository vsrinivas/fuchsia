// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_LEGACY_BIND_CONSTANTS_LEGACY_BIND_CONSTANTS_H_
#define LIB_DRIVER_LEGACY_BIND_CONSTANTS_LEGACY_BIND_CONSTANTS_H_

// LINT.IfChange
// global binding variables at 0x00XX
#define BIND_FLAGS 0x0000          // value of the flags register
#define BIND_PROTOCOL 0x0001       // primary protocol of the device
#define BIND_AUTOBIND 0x0002       // if this is an automated bind/load
#define BIND_COMPOSITE 0x003       // Whether this is a composite device
#define BIND_FIDL_PROTOCOL 0x0004  // primary FIDL protocol of the device

// pci binding variables at 0x01XX
#define BIND_PCI_VID 0x0100
#define BIND_PCI_DID 0x0101
#define BIND_PCI_CLASS 0x0102
#define BIND_PCI_SUBCLASS 0x0103
#define BIND_PCI_INTERFACE 0x0104
#define BIND_PCI_REVISION 0x0105
#define BIND_PCI_TOPO 0x0107

#define BIND_PCI_TOPO_PACK(bus, dev, func) (((bus) << 8) | (dev << 3) | (func))

// usb binding variables at 0x02XX
// these are used for both ZX_PROTOCOL_USB_INTERFACE and ZX_PROTOCOL_USB_FUNCTION
#define BIND_USB_VID 0x0200
#define BIND_USB_PID 0x0201
#define BIND_USB_CLASS 0x0202
#define BIND_USB_SUBCLASS 0x0203
#define BIND_USB_PROTOCOL 0x0204
#define BIND_USB_INTERFACE_NUMBER 0x0205

// Platform bus binding variables at 0x03XX
#define BIND_PLATFORM_DEV_VID 0x0300
#define BIND_PLATFORM_DEV_PID 0x0301
#define BIND_PLATFORM_DEV_DID 0x0302
#define BIND_PLATFORM_DEV_INSTANCE_ID 0x0304
#define BIND_PLATFORM_DEV_INTERRUPT_ID 0x0305

// ACPI binding variables at 0x04XX
#define BIND_ACPI_BUS_TYPE 0x0400
// Internal use only.
#define BIND_ACPI_ID 0x0401

// Intel HDA Codec binding variables at 0x05XX
#define BIND_IHDA_CODEC_VID 0x0500
#define BIND_IHDA_CODEC_DID 0x0501
#define BIND_IHDA_CODEC_MAJOR_REV 0x0502
#define BIND_IHDA_CODEC_MINOR_REV 0x0503
#define BIND_IHDA_CODEC_VENDOR_REV 0x0504
#define BIND_IHDA_CODEC_VENDOR_STEP 0x0505

// Serial binding variables at 0x06XX
#define BIND_SERIAL_CLASS 0x0600
#define BIND_SERIAL_VID 0x0601
#define BIND_SERIAL_PID 0x0602

// NAND binding variables at 0x07XX
#define BIND_NAND_CLASS 0x0700

// SDIO binding variables at 0x09XX
#define BIND_SDIO_VID 0x0900
#define BIND_SDIO_PID 0x0901
#define BIND_SDIO_FUNCTION 0x0902

// I2C binding variables at 0x0A0X
#define BIND_I2C_CLASS 0x0A00
#define BIND_I2C_BUS_ID 0x0A01
#define BIND_I2C_ADDRESS 0x0A02
#define BIND_I2C_VID 0x0A03
#define BIND_I2C_DID 0x0A04

// GPIO binding variables at 0x0A1X
#define BIND_GPIO_PIN 0x0A10

// POWER binding variables at 0x0A2X
#define BIND_POWER_DOMAIN 0x0A20
#define BIND_POWER_DOMAIN_COMPOSITE 0x0A21

// POWER binding variables at 0x0A3X
#define BIND_CLOCK_ID 0x0A30

// SPI binding variables at 0x0A4X
#define BIND_SPI_BUS_ID 0x0A41
#define BIND_SPI_CHIP_SELECT 0x0A42

// PWM binding variables at 0x0A5X
#define BIND_PWM_ID 0x0A50

// Init Step binding variables at 0x0A6X
#define BIND_INIT_STEP 0x0A60

// Codec binding variables at 0x0A7X
#define BIND_CODEC_INSTANCE 0x0A70

// Regsiters binding variables at 0x0A8X
#define BIND_REGISTER_ID 0x0A80

// Power sensor binding variables at 0x0A9X
#define BIND_POWER_SENSOR_DOMAIN 0x0A90
// LINT.ThenChange(/src/lib/ddk/include/lib/ddk/binding_priv.h)

#endif  // LIB_DRIVER_LEGACY_BIND_CONSTANTS_LEGACY_BIND_CONSTANTS_H_
