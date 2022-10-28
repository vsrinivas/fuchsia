// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_LIB_DDK_METADATA_H_
#define SRC_LIB_DDK_INCLUDE_LIB_DDK_METADATA_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/boot/image.h>

// This file contains metadata types for device_get_metadata()

// MAC Address for Ethernet, Wifi, Bluetooth, etc.
// Content: uint8_t[] (variable length based on type of MAC address)
#define DEVICE_METADATA_MAC_ADDRESS 0x43414D6D  // mMAC
static_assert(DEVICE_METADATA_MAC_ADDRESS == ZBI_TYPE_DRV_MAC_ADDRESS, "");

// Partition map for raw block device.
// Content: bootdata_partition_map_t
#define DEVICE_METADATA_PARTITION_MAP 0x5452506D  // mPRT
static_assert(DEVICE_METADATA_PARTITION_MAP == ZBI_TYPE_DRV_PARTITION_MAP, "");

// maximum size of DEVICE_METADATA_PARTITION_MAP data
#define METADATA_PARTITION_MAP_MAX 4096

// Initial USB mode
// type: usb_mode_t
#define DEVICE_METADATA_USB_MODE 0x4D425355  // USBM

#define DEVICE_METADATA_SERIAL_NUMBER 0x4e4c5253  // SRLN
static_assert(DEVICE_METADATA_SERIAL_NUMBER == ZBI_TYPE_SERIAL_NUMBER, "");

// Board-specific USB configuration
// type: UsbConfig
// fidl: usb-peripheral.fidl
#define DEVICE_METADATA_USB_CONFIG 0x4D425356  // USBC

// Serial port info
// type: serial_port_info_t
#define DEVICE_METADATA_SERIAL_PORT_INFO 0x4D524553  // SERM

// Platform board name (for sysinfo driver)
// type: char[ZBI_BOARD_NAME_LEN]
#define DEVICE_METADATA_BOARD_NAME 0x4E524F42  // BORN

// Platform board revision (for sysinfo driver)
// type: char[ZBI_BOARD_REVISION_LEN]
#define DEVICE_METADATA_BOARD_REVISION 0x52524F42  // BORR

// Platform board private data (for board driver)
// type: ???
#define DEVICE_METADATA_BOARD_PRIVATE 0x524F426D  // mBOR
static_assert(DEVICE_METADATA_BOARD_PRIVATE == ZBI_TYPE_DRV_BOARD_PRIVATE, "");

// Information that is sent through the isolated dev manager by a test.
// type: ??? - test defined
#define DEVICE_METADATA_TEST 0x54534554  // TEST

// Interrupt controller type (for sysinfo driver)
// type: uint8_t
#define DEVICE_METADATA_INTERRUPT_CONTROLLER_TYPE 0x43544E49  // INTC

// GUID map (for GPT driver)
// type: array of guid_map_t
#define DEVICE_METADATA_GUID_MAP 0x44495547  // GUID
#define DEVICE_METADATA_GUID_MAP_MAX_ENTRIES 16

// list of buttons_button_config_t
#define DEVICE_METADATA_BUTTONS_BUTTONS 0x424E5442  // BTNB

// list of buttons_gpio_config_t
#define DEVICE_METADATA_BUTTONS_GPIOS 0x474E5442  // BTNG

// list of char[ZX_MAX_NAME_LEN]
#define DEVICE_METADATA_NAME 0x454D414E  // NAME

// type: fuchsia_hardware_thermal_ThermalDeviceInfo
#define DEVICE_METADATA_THERMAL_CONFIG 0x54485243  // THRC

// type: array of gpio_pin_t
#define DEVICE_METADATA_GPIO_PINS 0x4F495047  // GPIO

// type: FIDL fuchsia.hardware.gpio.init/GpioInitMetadata
#define DEVICE_METADATA_GPIO_INIT_STEPS 0x49495047  // GPII

// type: array of power_domain_t
#define DEVICE_METADATA_POWER_DOMAINS 0x52574F50  // POWR

// type: clock_id_t
#define DEVICE_METADATA_CLOCK_IDS 0x4B4F4C43  // CLOK

// type: pwm_idx_t
#define DEVICE_METADATA_PWM_IDS 0x004D5750  // PWM\0

// type: emmc_config_t
#define DEVICE_METADATA_EMMC_CONFIG 0x434D4D45  // EMMC

// type: vendor specific Wifi configuration
#define DEVICE_METADATA_WIFI_CONFIG 0x49464957  // WIFI

// type: eth_dev_metadata_t
#define DEVICE_METADATA_ETH_MAC_DEVICE 0x43414D45  // EMAC

// type: eth_dev_metadata_t
#define DEVICE_METADATA_ETH_PHY_DEVICE 0x59485045  // EPHY

// type: FIDL fuchsia.hardware.i2c/I2CBusMetadata
#define DEVICE_METADATA_I2C_CHANNELS 0x43433249  // I2CC
// type: FIDL fuchsia.hardware.i2c/I2CChannel
#define DEVICE_METADATA_I2C_DEVICE 0x44433249  // I2CD

// type: FIDL fuchsia.hardware.spi.SpiBusMetadata
#define DEVICE_METADATA_SPI_CHANNELS 0x43495053  // SPIC

// type: display_driver_t
#define DEVICE_METADATA_DISPLAY_DEVICE 0x4C505344  // DSPL

// type: display_config_t
#define DEVICE_METADATA_DISPLAY_CONFIG 0x43505344  // DSPC

// Maximum screen brightness in nits. Used by the backlight driver.
// type: double
#define DEVICE_METADATA_BACKLIGHT_MAX_BRIGHTNESS_NITS 0x4C4B4342  // BCKL

// list of lights_config_t
#define DEVICE_METADATA_LIGHTS 0x5448474C             // LGHT
#define DEVICE_METADATA_LIGHTS_GROUP_NAME 0x4E505247  // GRPN

// Encodings of Metadata for fuchsia::hardware::registers::Metadata
// list of linearized bytes as in fidl_outgoing_msg_t
#define DEVICE_METADATA_REGISTERS 0x53474552  // REGS

// Encodings of Metadata for fuchsia::hardware::vreg::Metadata
// list of linearized bytes as in fidl_outgoing_msg_t
#define DEVICE_METADATA_VREG 0x47455256  // VREG

// type: FIDL fuchsia.hardware.tee/TeeMetadata
#define DEVICE_METADATA_TEE_THREAD_CONFIG 0x43454554  // TEEC

// Metadata types that have least significant byte set to lowercase 'd'
// signify private driver data.
// This allows creating metadata types to be defined local to a particular
// driver or driver protocol.
#define DEVICE_METADATA_PRIVATE 0x00000064
#define DEVICE_METADATA_PRIVATE_MASK 0x000000ff

static inline bool is_private_metadata(uint32_t type) {
  return ((type & DEVICE_METADATA_PRIVATE_MASK) == DEVICE_METADATA_PRIVATE);
}

#endif  // SRC_LIB_DDK_INCLUDE_LIB_DDK_METADATA_H_
