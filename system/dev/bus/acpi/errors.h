// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <acpica/acpi.h>

static inline mx_status_t acpi_to_mx_status(ACPI_STATUS acpi_status) {
    switch (acpi_status) {
    case AE_ERROR:
    case AE_NO_ACPI_TABLES:
        return MX_ERR_INTERNAL;
    case AE_NO_NAMESPACE:
        return MX_ERR_NOT_FOUND;
    case AE_NO_MEMORY:
        return MX_ERR_NO_MEMORY;
    case AE_NOT_EXIST:
        return MX_ERR_NOT_FOUND;
    case AE_ALREADY_EXISTS:
        return MX_ERR_ALREADY_EXISTS;
    case AE_TYPE:
        return MX_ERR_WRONG_TYPE;
    case AE_NULL_OBJECT:
    case AE_NULL_ENTRY:
        return MX_ERR_NOT_FOUND;
    case AE_BUFFER_OVERFLOW:
        return MX_ERR_BUFFER_TOO_SMALL;
    case AE_STACK_OVERFLOW:
    case AE_STACK_UNDERFLOW:
        return MX_ERR_INTERNAL;
    case AE_NOT_IMPLEMENTED:
    case AE_SUPPORT:
        return MX_ERR_NOT_SUPPORTED;
    case AE_LIMIT:
        return MX_ERR_INTERNAL;
    case AE_TIME:
        return MX_ERR_TIMED_OUT;
    case AE_ACQUIRE_DEADLOCK:
    case AE_RELEASE_DEADLOCK:
    case AE_NOT_ACQUIRED:
    case AE_ALREADY_ACQUIRED:
        return MX_ERR_INTERNAL;
    case AE_NO_HARDWARE_RESPONSE:
        return MX_ERR_TIMED_OUT;
    case AE_NO_GLOBAL_LOCK:
        return MX_ERR_INTERNAL;
    case AE_ABORT_METHOD:
        return MX_ERR_INTERNAL;
    case AE_SAME_HANDLER:
        return MX_ERR_ALREADY_EXISTS;
    case AE_OWNER_ID_LIMIT:
        return MX_ERR_NO_RESOURCES;
    case AE_NOT_CONFIGURED:
        return MX_ERR_NOT_FOUND;
    case AE_ACCESS:
        return MX_ERR_ACCESS_DENIED;
    case AE_IO_ERROR:
        return MX_ERR_IO;
    default:
        return MX_ERR_INTERNAL;
    }
}
