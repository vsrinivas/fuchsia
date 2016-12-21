// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <acpica/acpi.h>
#include <magenta/listnode.h>
#include <magenta/types.h>

mx_status_t resource_tree_init(mx_handle_t port, mx_handle_t acpi_bus_resource);

typedef struct {
    list_node_t list_node;

    ACPI_HANDLE acpi_handle;
    mx_handle_t resource_handle;
} resource_node_t;
