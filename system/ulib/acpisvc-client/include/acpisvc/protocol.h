// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/syscalls-types.h>

#define ACPI_MAX_REQUEST_SIZE 2048
#define ACPI_MAX_RESPONSE_SIZE 2048

enum {
    ACPI_CMD_NEW_CONNECTION = 0,
    ACPI_CMD_LIST_CHILDREN = 1,
    ACPI_CMD_GET_CHILD_HANDLE = 2,
    ACPI_CMD_GET_PCI_INIT_ARG = 3,
    ACPI_CMD_S_STATE_TRANSITION = 4,
    ACPI_CMD_PS0 = 5,
};

typedef struct {
    uint32_t len;    // Total length, including header
    uint16_t cmd;    // CMD code from above enum
    uint8_t version; // Protocol version, currently only 0 defined.
    uint8_t _reserved;
    uint32_t request_id; // ID value that will be echo'd back
} __PACKED acpi_cmd_hdr_t;

typedef struct {
    mx_status_t status;
    uint32_t len;
    uint32_t request_id; // ID value that was sent in cmd
} __PACKED acpi_rsp_hdr_t;

// List all children of the node associated with the handle used to issue the
// request.
typedef struct {
    acpi_cmd_hdr_t hdr;
} __PACKED acpi_cmd_list_children_t;
typedef struct {
    acpi_rsp_hdr_t hdr;

    uint32_t num_children;
    struct {
        // All of these values are non-NULL terminated.  name is a unique
        // identifier (scoped to the handle associated with the request)
        // that may be used to request a handle to a child below.
        char name[4];
        char hid[8];
        // We return the first 4 PNP/ACPI IDs found in the CID list
        char cid[4][8];
    } children[];
} __PACKED acpi_rsp_list_children_t;

// Request a handle to a child node by name.
typedef struct {
    acpi_cmd_hdr_t hdr;

    // name is not NULL terminated
    char name[4];
} __PACKED acpi_cmd_get_child_handle_t;
typedef struct {
    acpi_rsp_hdr_t hdr;
} __PACKED acpi_rsp_get_child_handle_t;

// Request information for initializing a PCIe bus.  Only valid if
// the associated node corresponds to a PCI root bridge.
typedef struct {
    acpi_cmd_hdr_t hdr;
} __PACKED acpi_cmd_get_pci_init_arg_t;
typedef struct {
    acpi_rsp_hdr_t hdr;

    mx_pci_init_arg_t arg;
} __PACKED acpi_rsp_get_pci_init_arg_t;

// Perform an S-state transition (S5: poweroff, S3: suspend-to-RAM).
// Also supports reboots.
enum {
    ACPI_S_STATE_REBOOT = 1,
    ACPI_S_STATE_S3 = 2,
    ACPI_S_STATE_S5 = 3,
};
typedef struct {
    acpi_cmd_hdr_t hdr;

    uint8_t target_state;
} __PACKED acpi_cmd_s_state_transition_t;
typedef struct {
    acpi_rsp_hdr_t hdr;
} __PACKED acpi_rsp_s_state_transition_t;

typedef struct {
    acpi_cmd_hdr_t hdr;
    // name must be NULL terminated
    char name[1024];
} __PACKED acpi_cmd_ps0_t;
typedef struct {
    acpi_rsp_hdr_t hdr;
} __PACKED acpi_rsp_ps0_t;
