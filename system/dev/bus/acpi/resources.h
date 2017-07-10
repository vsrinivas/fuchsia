// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <acpica/acpi.h>
#include <magenta/types.h>

enum resource_address_type {
    RESOURCE_ADDRESS_MEMORY,
    RESOURCE_ADDRESS_IO,
    RESOURCE_ADDRESS_BUS_NUMBER,
    RESOURCE_ADDRESS_UNKNOWN,
};

// Structure that unifies the 3 Memory resource types
typedef struct resource_memory {
    bool writeable;
    uint32_t minimum; // min base address
    uint32_t maximum; // max base address
    uint32_t alignment;
    uint32_t address_length;
} resource_memory_t;

// Structure that unifies the 4 Address resource types
typedef struct resource_address {
    // Interpretation of min/max depend on the min/max_address_fixed flags
    // below.
    uint64_t minimum;
    uint64_t maximum;
    uint64_t address_length;

    uint64_t translation_offset;
    uint64_t granularity;

    enum resource_address_type resource_type;
    bool consumed_only;
    bool subtractive_decode;
    bool min_address_fixed;
    bool max_address_fixed;
} resource_address_t;

typedef struct resource_io {
    bool decodes_full_space; // If false, only decodes 10-bits
    uint8_t alignment;
    uint8_t address_length;
    uint16_t minimum;
    uint16_t maximum;
} resource_io_t;

static bool resource_is_memory(ACPI_RESOURCE* res) {
    return res->Type == ACPI_RESOURCE_TYPE_MEMORY24 ||
            res->Type == ACPI_RESOURCE_TYPE_MEMORY32||
            res->Type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32;
}

static bool resource_is_address(ACPI_RESOURCE* res) {
    return res->Type == ACPI_RESOURCE_TYPE_ADDRESS16 ||
            res->Type == ACPI_RESOURCE_TYPE_ADDRESS32 ||
            res->Type == ACPI_RESOURCE_TYPE_ADDRESS64 ||
            res->Type == ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64;
}

static bool resource_is_io(ACPI_RESOURCE* res) {
    return res->Type == ACPI_RESOURCE_TYPE_IO ||
            res->Type == ACPI_RESOURCE_TYPE_FIXED_IO;
}

mx_status_t resource_parse_memory(ACPI_RESOURCE* res, resource_memory_t* out);
mx_status_t resource_parse_address(ACPI_RESOURCE* res, resource_address_t* out);
mx_status_t resource_parse_io(ACPI_RESOURCE* res, resource_io_t* out);
