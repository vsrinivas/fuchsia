// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <acpisvc/simple.h>

#include <stdlib.h>
#include <string.h>

#include <magenta/syscalls.h>

#define MAX_RETURNED_HANDLES 1

// Wait for the message with the given request id.
//
// *response* is a pointer to where to store the response.
// *len* is a pointer to where to store the length of the response.
// *handles* is a pointer to an array populated by wait_for_message.
// *num_handles* is an in/out parameter of the number of elements in
// the *handles* array, and is populated with the number of elements
// written.
static mx_status_t wait_for_message(
    mx_handle_t h, uint32_t req_id,
    void** response, size_t* len,
    mx_handle_t* handles, size_t* num_handles) {

    mx_status_t status = mx_handle_wait_one(h,
                                            MX_SIGNAL_READABLE,
                                            MX_TIME_INFINITE,
                                            NULL);
    if (status != NO_ERROR) {
        return status;
    }

    uint32_t rsp_len = 0;
    uint32_t num_handles_returned = 0;
    status = mx_msgpipe_read(h, NULL, &rsp_len, NULL, &num_handles_returned, 0);
    if (status != ERR_BUFFER_TOO_SMALL) {
        return status;
    }
    if (rsp_len < sizeof(acpi_rsp_hdr_t)) {
        return ERR_BAD_STATE;
    }
    if (num_handles_returned > MAX_RETURNED_HANDLES) {
        return ERR_BAD_STATE;
    }

    acpi_rsp_hdr_t* rsp = malloc(rsp_len);
    if (!rsp) {
        return ERR_NO_MEMORY;
    }

    mx_handle_t handles_returned[MAX_RETURNED_HANDLES];
    status = mx_msgpipe_read(h, rsp, &rsp_len, handles_returned, &num_handles_returned, 0);
    if (status != NO_ERROR) {
        free(rsp);
        return status;
    }

    if (rsp_len < sizeof(*rsp) ||
        rsp->request_id != req_id ||
        *num_handles < num_handles_returned) {

        free(rsp);
        for (uint32_t i = 0; i < num_handles_returned; ++i) {
            mx_handle_close(handles_returned[i]);
        }
        return ERR_BAD_STATE;
    }

    *response = rsp;
    *len = rsp_len;
    *num_handles = num_handles_returned;
    memcpy(handles, handles_returned,
           sizeof(mx_handle_t) * num_handles_returned);

    return NO_ERROR;
}

// Execute one round of the command-response protocol
//
// *cmd* is pointer to the cmd buffer.  The request_id will be populated in this
// function.
// *rsp* is a pointer to where to store the rsp buffer.
// *rsp_handles* is an array of handles to be populated
// *num_rsp_handles* is the size of that array
//
// This function will return an error if:
// - There was a problem sending the command or receiving response.
// - The response was an error response.
// - The response was malformed.
// - The response had an unexpected number of handles
static mx_status_t run_txn(
    acpi_handle_t* h,
    void* cmd, size_t cmd_len,
    void** rsp, size_t* rsp_len,
    mx_handle_t cmd_handle,
    mx_handle_t* rsp_handles, size_t num_rsp_handles) {

    if (cmd_len < sizeof(acpi_cmd_hdr_t)) {
        return ERR_INVALID_ARGS;
    }

    *rsp = NULL;

    mtx_lock(&h->lock);

    uint32_t req_id = h->next_req_id++;

    acpi_cmd_hdr_t* cmd_hdr = cmd;
    cmd_hdr->request_id = req_id;

    mx_status_t status = mx_msgpipe_write(h->pipe, cmd, cmd_len, &cmd_handle, (cmd_handle > 0) ? 1 : 0, 0);
    if (status != NO_ERROR) {
        if (cmd_handle) {
            mx_handle_close(cmd_handle);
        }
        goto exit;
    }

    size_t handle_count = num_rsp_handles;
    status = wait_for_message(h->pipe, req_id, rsp, rsp_len, rsp_handles, &handle_count);
    if (status != NO_ERROR) {
        goto exit;
    }

    acpi_rsp_hdr_t* rsp_hdr = *(acpi_rsp_hdr_t**)rsp;

    // Validate the response
    if (rsp_hdr->status != NO_ERROR) {
        status = rsp_hdr->status;
        goto cleanup;
    }
    if (rsp_hdr->len != *rsp_len) {
        status = ERR_BAD_STATE;
        goto cleanup;
    }
    if (handle_count != num_rsp_handles) {
        status = ERR_BAD_STATE;
        goto cleanup;
    }

    status = NO_ERROR;
    goto exit;

cleanup:
    for (uint32_t i = 0; i < handle_count; ++i) {
        mx_handle_close(rsp_handles[i]);
        rsp_handles[i] = 0;
    }

    free(*rsp);
    *rsp = NULL;
exit:
    mtx_unlock(&h->lock);
    return status;
}

mx_status_t acpi_list_children(acpi_handle_t* h,
                               acpi_rsp_list_children_t** response, size_t* len) {

    acpi_cmd_list_children_t cmd = {
        .hdr = {
            .version = 0,
            .cmd = ACPI_CMD_LIST_CHILDREN,
            .len = sizeof(cmd),
        },
    };

    acpi_rsp_list_children_t* rsp;
    size_t rsp_len;
    mx_status_t status =
        run_txn(h, &cmd, sizeof(cmd), (void**)&rsp, &rsp_len, 0, NULL, 0);
    if (status != NO_ERROR) {
        return status;
    }

    // Validate the response
    if (rsp_len != sizeof(*rsp) + sizeof(rsp->children[0]) * rsp->num_children) {
        free(rsp);
        return ERR_BAD_STATE;
    }

    *response = rsp;
    *len = rsp_len;
    return NO_ERROR;
}

mx_status_t acpi_get_child_handle(acpi_handle_t* h, const char name[4],
                                  acpi_handle_t* child) {
    acpi_cmd_get_child_handle_t cmd = {
        .hdr = {
            .version = 0,
            .cmd = ACPI_CMD_GET_CHILD_HANDLE,
            .len = sizeof(cmd),
        },
    };
    memcpy(cmd.name, name, sizeof(cmd.name));

    acpi_rsp_get_child_handle_t* rsp = NULL;
    size_t rsp_len;

    mx_handle_t handles[1] = {0};
    mx_status_t status =
        run_txn(h, &cmd, sizeof(cmd), (void**)&rsp, &rsp_len, 0, handles, countof(handles));
    if (status != NO_ERROR) {
        return status;
    }

    acpi_handle_init(child, handles[0]);
    free(rsp);
    return NO_ERROR;
}

mx_status_t acpi_get_pci_init_arg(acpi_handle_t* h,
                                  acpi_rsp_get_pci_init_arg_t** response,
                                  size_t* len) {
    acpi_cmd_get_pci_init_arg_t cmd = {
        .hdr = {
            .version = 0,
            .cmd = ACPI_CMD_GET_PCI_INIT_ARG,
            .len = sizeof(cmd),
        },
    };

    acpi_rsp_get_pci_init_arg_t* rsp;
    size_t rsp_len;

    mx_status_t status =
        run_txn(h, &cmd, sizeof(cmd), (void**)&rsp, &rsp_len, 0, NULL, 0);
    if (status != NO_ERROR) {
        return status;
    }

    *response = rsp;
    *len = rsp_len;
    return NO_ERROR;
}

mx_status_t acpi_s_state_transition(acpi_handle_t* h, uint8_t target_state) {
    acpi_cmd_s_state_transition_t cmd = {
        .hdr = {
            .version = 0,
            .cmd = ACPI_CMD_S_STATE_TRANSITION,
            .len = sizeof(cmd),
        },
        .target_state = target_state,
    };

    acpi_rsp_s_state_transition_t* rsp;
    size_t rsp_len;
    mx_status_t status =
        run_txn(h, &cmd, sizeof(cmd), (void**)&rsp, &rsp_len, 0, NULL, 0);
    if (status != NO_ERROR) {
        return status;
    }

    // This state should be unreachable.
    abort();
}

mx_status_t acpi_ps0(acpi_handle_t* h, char* path, size_t len) {
    acpi_cmd_ps0_t cmd = {
        .hdr = {
            .version = 0,
            .cmd = ACPI_CMD_PS0,
            .len = sizeof(cmd),
        },
    };
    memcpy(cmd.name, path, len);

    acpi_rsp_ps0_t* rsp;
    size_t rsp_len;
    mx_status_t status =
        run_txn(h, &cmd, sizeof(cmd), (void**)&rsp, &rsp_len, 0, NULL, 0);
    if (status != NO_ERROR) {
        return status;
    }

    free(rsp);
    return NO_ERROR;
}

mx_handle_t acpi_clone_handle(acpi_handle_t* _h) {
    acpi_cmd_hdr_t cmd = {
        .version = 0,
        .cmd = ACPI_CMD_NEW_CONNECTION,
        .len = sizeof(cmd),
    };

    mx_handle_t h[2];
    mx_status_t status;
    if ((status = mx_msgpipe_create(h, 0)) < 0) {
        return status;
    }

    acpi_rsp_hdr_t* rsp;
    size_t rsp_len;
    if ((status = run_txn(_h, &cmd, sizeof(cmd), (void**)&rsp, &rsp_len, h[1], NULL, 0)) < 0) {
        return status;
    }
    free(rsp);
    return h[0];
}

