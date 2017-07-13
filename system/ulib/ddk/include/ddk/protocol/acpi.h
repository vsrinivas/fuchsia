// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

// TODO: Temporary api to get ACPI going.
// Use fidl to generate?

__BEGIN_CDECLS;

typedef struct acpi_rsp_bif {
    uint32_t power_unit;
    uint32_t design_capacity;
    uint32_t last_full_charge_capacity;
    uint32_t battery_technology;
    uint32_t design_voltage;
    uint32_t design_capacity_of_warning;
    uint32_t design_capacity_of_low;
    uint32_t battery_capacity_granularity;
    uint32_t battery_capacity_granularity_2;
    char model_number[32];
    char serial_number[32];
    char battery_type[32];
    char oem_info[32];
} acpi_rsp_bif_t;

typedef struct acpi_rsp_bst {
    uint32_t battery_state;
    uint32_t battery_present_rate;
    uint32_t battery_remaining_capacity;
    uint32_t battery_present_voltage;
} acpi_rsp_bst_t;

typedef struct acpi_protocol_ops {
    mx_status_t (*_BIF)(void* ctx, acpi_rsp_bif_t* rsp);
    mx_status_t (*_BST)(void* ctx, acpi_rsp_bst_t* rsp);
} acpi_protocol_ops_t;

typedef struct acpi_protocol {
    acpi_protocol_ops_t* ops;
    void* ctx;
} acpi_protocol_t;

static inline mx_status_t acpi_BIF(acpi_protocol_t* acpi, acpi_rsp_bif_t* rsp) {
    return acpi->ops->_BIF(acpi->ctx, rsp);
}

static inline mx_status_t acpi_BST(acpi_protocol_t* acpi, acpi_rsp_bst_t* rsp) {
    return acpi->ops->_BST(acpi->ctx, rsp);
}

__END_CDECLS;
