// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <acpisvc/protocol.h>
#include <magenta/compiler.h>
#include <magenta/syscalls.h>

__BEGIN_CDECLS

// This header provides simple blocking calls to acpisvc.  The functions
// will discard any extra responses they find on the message pipe, so
// these simplified functions should not be mixed with other interfaces.

typedef struct {
    mx_handle_t pipe;
    mtx_t lock;
    uint32_t next_req_id;
} acpi_handle_t;

static void acpi_handle_init(acpi_handle_t* h, mx_handle_t pipe) {
    h->pipe = pipe;
    h->lock = (mtx_t)MTX_INIT;
}

static void acpi_handle_close(acpi_handle_t* h) {
    mtx_lock(&h->lock);
    mx_handle_close(h->pipe);
    mtx_unlock(&h->lock);
}

// Obtain an additional acpi service handle
//
mx_handle_t acpi_clone_handle(acpi_handle_t* h);

// List the children of the ACPI node.
//
// *rsp* is a pointer to store the response into.  The response can be released
// with free().
// *len* is a pointer to store the length of the response into
mx_status_t acpi_list_children(acpi_handle_t* h,
                               acpi_rsp_list_children_t** rsp, size_t* len);

// Get a handle to the specified child of the ACPI node.
//
// *name* is a 4-character name returned from list_children().
// *child* will become a handle to the child, if the call is successful.
mx_status_t acpi_get_child_handle(acpi_handle_t* h, const char name[4],
                                  acpi_handle_t* child);

// Get information necessary for PCI bus driver initialization.
//
// *rsp* is a pointer to store the response into.  The response can be released
// with free().
// *len* is a pointer to store the length of the response into
//
// This command will only succeed if the ACPI node represents a PCI root bus.
mx_status_t acpi_get_pci_init_arg(acpi_handle_t* h,
                                  acpi_rsp_get_pci_init_arg_t** response,
                                  size_t* len);

// Change the system's power state.
//
// This command will only succeed if the handle is the ACPI root handle.
// TODO(teisenbe): Perhaps open this up to a different handle.
mx_status_t acpi_s_state_transition(acpi_handle_t* h, uint8_t target_state);

// Execute PS0 for an ACPI node.
//
// *path* is a full path to an ACPI object.
// NOTE: this is a temporary interface that will be removed soon.
mx_status_t acpi_ps0(acpi_handle_t* h, char* path, size_t len);

__END_CDECLS
