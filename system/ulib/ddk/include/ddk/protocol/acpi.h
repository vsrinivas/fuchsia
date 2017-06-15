// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <ddk/driver.h>

__BEGIN_CDECLS;

/**
 * protocol/acpi.h - ACPI protocol definitions
 *
 * FIXME(yky,teisenbe): not the real thing
 */

typedef struct acpi_protocol_ops {
    mx_handle_t (*clone_handle)(mx_device_t* dev);
} acpi_protocol_ops_t;

typedef struct acpi_protocol {
    acpi_protocol_ops_t* ops;
    void* ctx;
} acpi_protocol_t;
