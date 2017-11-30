// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <acpica/acpi.h>

ACPI_STATUS acpi_evaluate_integer(ACPI_HANDLE handle, const char* name, uint64_t* out) {
    ACPI_OBJECT obj = {
        .Type = ACPI_TYPE_INTEGER,
    };
    ACPI_BUFFER buffer = {
        .Length = sizeof(obj),
        .Pointer = &obj,
    };
    ACPI_STATUS acpi_status = AcpiEvaluateObject(handle, (char*)name, NULL, &buffer);
    if (acpi_status != AE_OK) {
        return acpi_status;
    }
    *out = obj.Integer.Value;
    return AE_OK;
}

ACPI_STATUS acpi_evaluate_method_intarg(ACPI_HANDLE handle, const char* name, uint64_t arg) {
    ACPI_OBJECT obj = {
        .Integer.Type = ACPI_TYPE_INTEGER,
        .Integer.Value = arg,
    };
    ACPI_OBJECT_LIST params = {
        .Count = 1,
        .Pointer = &obj,
    };
    return AcpiEvaluateObject(handle, (char*)name, &params, NULL);
}
