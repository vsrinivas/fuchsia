// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resources.h"

mx_status_t resource_parse_memory(ACPI_RESOURCE* res, resource_memory_t* out) {
    switch (res->Type) {
        case ACPI_RESOURCE_TYPE_MEMORY24: {
            ACPI_RESOURCE_MEMORY24* m24 = &res->Data.Memory24;
            out->writeable = !m24->WriteProtect;
            out->minimum = (uint32_t)m24->Minimum << 8;
            out->maximum = (uint32_t)m24->Maximum << 8;
            out->alignment = m24->Alignment ? m24->Alignment : 1U<<16;
            out->address_length = (uint32_t)m24->AddressLength << 8;
            break;
        }
        case ACPI_RESOURCE_TYPE_MEMORY32: {
            ACPI_RESOURCE_MEMORY32* m32 = &res->Data.Memory32;
            out->writeable = !m32->WriteProtect;
            out->minimum = m32->Minimum;
            out->maximum = m32->Maximum;
            out->alignment = m32->Alignment;
            out->address_length = m32->AddressLength;
            break;
        }
        case ACPI_RESOURCE_TYPE_FIXED_MEMORY32: {
            ACPI_RESOURCE_FIXED_MEMORY32* m32 = &res->Data.FixedMemory32;
            out->writeable = !m32->WriteProtect;
            out->minimum = m32->Address;
            out->maximum = m32->Address;
            out->alignment = 1;
            out->address_length = m32->AddressLength;
            break;
        }
        default: return MX_ERR_INVALID_ARGS;
    }

    return MX_OK;
}

#define EXTRACT_ADDRESS_FIELDS(src, out) do { \
            (out)->minimum = (src)->Address.Minimum; \
            (out)->maximum = (src)->Address.Maximum; \
            (out)->address_length = (src)->Address.AddressLength; \
            (out)->translation_offset = (src)->Address.TranslationOffset; \
            (out)->granularity = (src)->Address.Granularity; \
            (out)->consumed_only = ((src)->ProducerConsumer == ACPI_CONSUMER); \
            (out)->subtractive_decode = ((src)->Decode == ACPI_SUB_DECODE); \
            (out)->min_address_fixed = (src)->MinAddressFixed; \
            (out)->max_address_fixed = (src)->MaxAddressFixed; \
} while (0)

mx_status_t resource_parse_address(ACPI_RESOURCE* res, resource_address_t* out) {
    uint8_t resource_type;
    switch (res->Type) {
        case ACPI_RESOURCE_TYPE_ADDRESS16: {
            ACPI_RESOURCE_ADDRESS16* a16 = &res->Data.Address16;
            EXTRACT_ADDRESS_FIELDS(a16, out);
            resource_type = a16->ResourceType;
            break;
        }
        case ACPI_RESOURCE_TYPE_ADDRESS32: {
            ACPI_RESOURCE_ADDRESS32* a32 = &res->Data.Address32;
            EXTRACT_ADDRESS_FIELDS(a32, out);
            resource_type = a32->ResourceType;
            break;
        }
        case ACPI_RESOURCE_TYPE_ADDRESS64: {
            ACPI_RESOURCE_ADDRESS64* a64 = &res->Data.Address64;
            EXTRACT_ADDRESS_FIELDS(a64, out);
            resource_type = a64->ResourceType;
            break;
        }
        case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64: {
            ACPI_RESOURCE_EXTENDED_ADDRESS64* a64 = &res->Data.ExtAddress64;
            EXTRACT_ADDRESS_FIELDS(a64, out);
            resource_type = a64->ResourceType;
            break;
        }
        default: return MX_ERR_INVALID_ARGS;
    }

    switch (resource_type) {
        case ACPI_MEMORY_RANGE: out->resource_type = RESOURCE_ADDRESS_MEMORY; break;
        case ACPI_IO_RANGE: out->resource_type = RESOURCE_ADDRESS_IO; break;
        case ACPI_BUS_NUMBER_RANGE: out->resource_type = RESOURCE_ADDRESS_BUS_NUMBER; break;
        default: out->resource_type = RESOURCE_ADDRESS_UNKNOWN;
    }

    return MX_OK;
}

mx_status_t resource_parse_io(ACPI_RESOURCE* res, resource_io_t* out) {
    switch (res->Type) {
        case ACPI_RESOURCE_TYPE_IO: {
            ACPI_RESOURCE_IO* io = &res->Data.Io;
            out->decodes_full_space = (io->IoDecode == ACPI_DECODE_16);
            out->alignment = io->Alignment;
            out->address_length = io->AddressLength;
            out->minimum = io->Minimum;
            out->maximum = io->Maximum;
            break;
        }
        case ACPI_RESOURCE_TYPE_FIXED_IO: {
            ACPI_RESOURCE_FIXED_IO* io = &res->Data.FixedIo;
            out->decodes_full_space = false;
            out->alignment = 1;
            out->address_length = io->AddressLength;
            out->minimum = io->Address;
            out->maximum = io->Address;
            break;
        }
        default: return MX_ERR_INVALID_ARGS;
    }

    return MX_OK;
}
