// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resource_tree.h"

#include <assert.h>
#include <stdio.h>

#include <acpica/acpi.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/resource.h>

#define MAX_NAMESPACE_DEPTH 100

static list_node_t resource_list = LIST_INITIAL_VALUE(resource_list);

static void populate_resource_records(ACPI_DEVICE_INFO* info, mx_rrec_t records[3]);
static ACPI_STATUS resource_tree_init_callback(ACPI_HANDLE object,
                                                    uint32_t nesting_level,
                                                    void* context,
                                                    void **retval);
static ACPI_STATUS resource_tree_init_ascending_callback(ACPI_HANDLE object,
                                                              uint32_t nesting_level,
                                                              void* context,
                                                              void **retval);

typedef struct {
    // Port to bind all of the resources to (so we can wait for connect()s).
    mx_handle_t port;
    // Scratch space for storing stack of resources in our DFS traversal
    mx_handle_t parent_resources[MAX_NAMESPACE_DEPTH + 1];
} resource_tree_context_t;

mx_status_t resource_tree_init(mx_handle_t port, mx_handle_t acpi_bus_resource) {
    assert(list_is_empty(&resource_list));

    resource_tree_context_t* context = malloc(sizeof(*context));
    if (context == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    context->port = port;
    for (size_t i = 1; i < countof(context->parent_resources); ++i) {
        context->parent_resources[i] = MX_HANDLE_INVALID;
    }
    context->parent_resources[0] = acpi_bus_resource;

    mx_status_t status = MX_OK;
    ACPI_STATUS acpi_status = AcpiWalkNamespace(ACPI_TYPE_DEVICE,
                                                ACPI_ROOT_OBJECT,
                                                MAX_NAMESPACE_DEPTH,
                                                resource_tree_init_callback,
                                                resource_tree_init_ascending_callback,
                                                context,
                                                (void**)&status);
    // Sanity check our bookkeeping
    if (acpi_status == AE_OK && status == MX_OK) {
        for (size_t i = 1; i < countof(context->parent_resources); ++i) {
            assert(context->parent_resources[i] == MX_HANDLE_INVALID);
        }
    }
    free(context);

    if (acpi_status != AE_OK) {
        status = MX_ERR_BAD_STATE;
        goto err;
    }

    if (status != MX_OK) {
        goto err;
    }
    return MX_OK;

err:
    while (!list_is_empty(&resource_list)) {
        resource_node_t* node = list_remove_head_type(&resource_list, resource_node_t, list_node);
        mx_handle_close(node->resource_handle);
        free(node);
    }
    return status;
}

static void populate_resource_records(ACPI_DEVICE_INFO* info, mx_rrec_t records[3]) {
    // Each device resource will have 3 records:
    // 1) The required self entry
    // 2) A u64 data entry containing the ACPI HID and ADR
    // 3) A u64 data entry containing the ACPI CID(s)

    // Create the self record
    records[0].self.type = MX_RREC_SELF;
    records[0].self.subtype = MX_RREC_SELF_GENERIC;
    records[0].self.options = 0;
    records[0].self.record_count = 3;
    snprintf(records[0].self.name, sizeof(records[0].self.name), "ACPI:%4s", (char*)&info->Name);

    // Create the HID/ADR record
    records[1].data.type = MX_RREC_DATA;
    records[1].data.subtype = MX_RREC_DATA_U64;
    records[1].data.options = 2; // count
    if ((info->Valid & ACPI_VALID_HID) &&
        info->HardwareId.Length > 0 && info->HardwareId.Length <= sizeof(uint64_t)) {

        strncpy((char*)&records[1].data.u64[0], info->HardwareId.String, sizeof(uint64_t));
    } else {
        records[1].data.u64[0] = UINT64_MAX; // invalid HID, since they are alphanumeric
    }

    if (info->Valid & ACPI_VALID_ADR) {
        records[1].data.u64[1] = info->Address;
    } else {
        records[1].data.u64[1] = UINT64_MAX;
    }

    // Create the CIDs record
    records[2].data.type = MX_RREC_DATA;
    records[2].data.subtype = MX_RREC_DATA_U64;
    if (info->Valid & ACPI_VALID_CID) {
        size_t count = info->CompatibleIdList.Count;
        if (count > countof(records[2].data.u64)) {
            // TODO(teisenbe): Perhaps do more than just truncate here; I have
            // not seen any devices with a large number of CIDs though, and the
            // standard requires they be ordered by highest preference first.
            count = countof(records[2].data.u64);
        }
        records[2].data.options = count;
        for (size_t i = 0; i < count; ++i) {
            ACPI_PNP_DEVICE_ID* id = &info->CompatibleIdList.Ids[i];

            if (id->Length > 0 && id->Length <= sizeof(uint64_t)) {
                strncpy((char*)&records[2].data.u64[i], id->String, sizeof(uint64_t));
            } else {
                records[2].data.u64[i] = UINT64_MAX;
            }
        }
    } else {
        records[2].data.options = 0; // count
    }
}

static ACPI_STATUS resource_tree_init_callback(ACPI_HANDLE object,
                                                    uint32_t nesting_level,
                                                    void* context,
                                                    void **retval) {
    assert(nesting_level < MAX_NAMESPACE_DEPTH);
    resource_tree_context_t* ctx = context;
    mx_status_t* ret = (mx_status_t*)retval;

    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS acpi_status = AcpiGetObjectInfo(object, &info);
    if (acpi_status != AE_OK) {
        return acpi_status;
    }

    mx_rrec_t records[3] = { { 0 } };
    populate_resource_records(info, records);

    ACPI_FREE(info);

    // Find the closest parent (there may be gaps due to intermediate
    // non-devices in the ACPI namespace).
    mx_handle_t parent = MX_HANDLE_INVALID;
    for (int64_t i = nesting_level; i >= 0 && parent == MX_HANDLE_INVALID; --i) {
        parent = ctx->parent_resources[i];
    }
    assert(parent != MX_HANDLE_INVALID);

    resource_node_t* node = malloc(sizeof(*node));
    node->acpi_handle = object;

    mx_handle_t resource = MX_HANDLE_INVALID;
    mx_status_t status = mx_resource_create(parent, records, countof(records), &resource);
    if (status != MX_OK) {
        printf("ACPI: Failed to create node for %p: %d\n", object, status);
        goto err;
    }
    node->resource_handle = resource;

#if 0 // Disabled until we support binding to resources with MX_RESOURCE_READABLE
    status = mx_port_bind(ctx->port, (uint64_t)node, resource, MX_RESOURCE_READABLE);
    if (status != MX_OK) {
        printf("ACPI: Failed to bind node %p to port: %d\n", object, status);
        mx_handle_close(resource);
        goto err;
    }
#endif

    list_add_tail(&resource_list, &node->list_node);

    assert(ctx->parent_resources[nesting_level + 1] == MX_HANDLE_INVALID);
    ctx->parent_resources[nesting_level + 1] = resource;
    return AE_OK;

err:
    mx_handle_close(resource);
    free(node);
    *ret = status;
    return AE_CTRL_TERMINATE;
}

// The AcpiWalkNamespace function performs a DFS; on our way back down a branch,
// clear out the handles.  We don't close them, so that when we received a
// notification on our port about a waiting connect(), the key will be the
// handle.
static ACPI_STATUS resource_tree_init_ascending_callback(ACPI_HANDLE object,
                                                              uint32_t nesting_level,
                                                              void* context,
                                                              void **retval) {
    assert(nesting_level < MAX_NAMESPACE_DEPTH);
    resource_tree_context_t* ctx = context;

    ctx->parent_resources[nesting_level + 1] = MX_HANDLE_INVALID;
    return AE_OK;
}
