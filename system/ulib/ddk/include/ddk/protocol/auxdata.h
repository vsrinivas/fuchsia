// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef enum {
    // returns the nth child of this device
    //  in: auxdata_args_nth_device_t
    // out: [depends on auxdata_args_nth_device_t.child_type]
    AUXDATA_NTH_DEVICE,

    // returns the nth child of a pci device
    //  in: auxdata_args_pci_child_nth_device_t
    // out: [depends on auxdata_args_pci_child_nth_device_t.child_type]
    AUXDATA_PCI_CHILD_NTH_DEVICE,

    // TODO: not implemented
    // returns the timing parameters of the i2c bus
    //  in: auxdata_i2c_timing_type_t timing
    // out: auxdata_i2c_timing_t i2c bus timing info
    AUXDATA_I2C_TIMING,

    AUXDATA_TYPE_MAX,
} auxdata_type_t;

// AUXDATA_NTH_DEVICE, AUXDATA_PCI_CHILD_NTH_DEVICE

typedef enum {
    // auxdata_i2c_device_t
    AUXDATA_DEVICE_I2C,
    AUXDATA_DEVICE_MAX,
} auxdata_device_type_t;

// args: AUXDATA_NTH_DEVICE

typedef struct {
    // type of the expected device
    auxdata_device_type_t child_type;
    // device index
    uint8_t n;
} auxdata_args_nth_device_t;

// args: AUXDATA_PCI_CHILD_NTH_DEVICE

typedef struct {
    // PCI device info
    uint8_t bus_id;
    uint8_t dev_id;
    uint8_t func_id;
    // device index
    uint8_t n;
    // type of the expected device
    auxdata_device_type_t child_type;
} auxdata_args_pci_child_nth_device_t;

// out: AUXDATA_NTH_DEVICE, AUXDATA_PCI_CHILD_NTH_DEVICE

typedef struct {
    // i2c bus config
    uint8_t bus_master;
    uint8_t ten_bit;
    uint16_t address;
    uint32_t bus_speed;
    // optional protocol id for this device
    uint32_t protocol_id;
} auxdata_i2c_device_t;

// args: AUXDATA_I2C_TIMING

typedef enum {
    AUXDATA_I2C_TIMING_SS,
    AUXDATA_I2C_TIMING_FP,
    AUXDATA_I2C_TIMING_HS,
    AUXDATA_I2C_TIMING_FM,
} auxdata_i2c_timing_type_t;

// out: AUXDATA_I2C_TIMING

typedef struct {
    uint16_t hcnt;
    uint16_t lcnt;
    uint32_t sda_hold_time;
} auxdata_i2c_timing_t;

__END_CDECLS;
