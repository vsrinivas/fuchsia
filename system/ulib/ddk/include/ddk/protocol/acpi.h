// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

typedef struct acpi_protocol_ops {
} acpi_protocol_ops_t;

typedef struct acpi_protocol {
    acpi_protocol_ops_t* ops;
    void* ctx;
} acpi_protocol_t;

__END_CDECLS;
