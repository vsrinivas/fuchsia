// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "processor.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include <acpica/acpi.h>
#include <acpisvc/protocol.h>
#include <magenta/syscalls.h>
#include <mxio/dispatcher.h>

#include "pci.h"
#include "power.h"

// Data associated with each message pipe handle
typedef struct {
    // The namespace node associated with this handle.  The
    // handle should be allowed to access ACPI resources further up
    // the namespace tree.
    ACPI_HANDLE ns_node;
    bool root_node;
} acpi_handle_ctx_t;

// Command functions.  These should return an error only if the connection
// should be aborted.  Otherwise they should send their own replies.
static mx_status_t cmd_list_children(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_get_child_handle(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_get_pci_init_arg(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_s_state_transition(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_ps0(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_new_connection(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);

typedef mx_status_t (*cmd_handler_t)(mx_handle_t, acpi_handle_ctx_t*, void*);
static const cmd_handler_t cmd_table[] = {
        [ACPI_CMD_LIST_CHILDREN] = cmd_list_children,
        [ACPI_CMD_GET_CHILD_HANDLE] = cmd_get_child_handle,
        [ACPI_CMD_GET_PCI_INIT_ARG] = cmd_get_pci_init_arg,
        [ACPI_CMD_S_STATE_TRANSITION] = cmd_s_state_transition,
        [ACPI_CMD_PS0] = cmd_ps0,
        [ACPI_CMD_NEW_CONNECTION] = cmd_new_connection,
};

static mx_status_t send_error(mx_handle_t h, uint32_t req_id, mx_status_t status);

static mxio_dispatcher_t* dispatcher;
static mx_status_t dispatch(mx_handle_t h, void* _ctx, void* cookie) {
    acpi_handle_ctx_t* ctx = _ctx;

    // Check if handle is closed
    if (h == 0) {
        free(ctx);
        return NO_ERROR;
    }

    uint32_t num_bytes = 0;
    uint32_t num_handles = 0;
    mx_status_t status = mx_msgpipe_read(h, NULL, &num_bytes, NULL, &num_handles, 0);
    if (status == ERR_BAD_STATE) {
        return ERR_DISPATCHER_NO_WORK;
    } else if (status != ERR_BUFFER_TOO_SMALL ||
               num_handles > 1 ||
               num_bytes > ACPI_MAX_REQUEST_SIZE) {
        // Trigger a close on our end
        return status;
    }

    mx_handle_t cmd_handle = 0;
    uint8_t buf[ACPI_MAX_REQUEST_SIZE];
    status = mx_msgpipe_read(h, buf, &num_bytes, &cmd_handle, &num_handles, 0);
    if (status != NO_ERROR) {
        goto cleanup;
    }

    // Validate we have at least a command header
    if (num_bytes < sizeof(acpi_cmd_hdr_t)) {
        status = ERR_INVALID_ARGS;
        goto cleanup;
    }

    acpi_cmd_hdr_t* hdr = (acpi_cmd_hdr_t*)buf;
    if (hdr->version != 0) {
        status = send_error(h, hdr->request_id, ERR_NOT_SUPPORTED);
        goto cleanup;
    }
    if (hdr->len != num_bytes) {
        status = send_error(h, hdr->request_id, ERR_INVALID_ARGS);
        goto cleanup;
    }

    // Dispatch the actual command
    if (hdr->cmd >= countof(cmd_table) || cmd_table[hdr->cmd] == NULL) {
        status = send_error(h, hdr->request_id, ERR_NOT_SUPPORTED);
        goto cleanup;
    }
    if (num_handles > 0) {
        if ((hdr->cmd != ACPI_CMD_NEW_CONNECTION) || (ctx->root_node == false)) {
            status = ERR_INVALID_ARGS;
            goto cleanup;
        }
        acpi_handle_ctx_t* context = calloc(1, sizeof(acpi_handle_ctx_t));
        if (context == NULL) {
            status = ERR_NO_MEMORY;
            goto cleanup;
        }
        context->root_node = true;
        if ((status = mxio_dispatcher_add(dispatcher, cmd_handle, context, NULL)) < 0) {
            free(context);
            goto cleanup;
        }
        acpi_rsp_hdr_t rsp;
        rsp.status = NO_ERROR;
        rsp.len = sizeof(rsp);
        rsp.request_id = hdr->request_id;
        return mx_msgpipe_write(h, &rsp, sizeof(rsp), NULL, 0, 0);
    }
    status = cmd_table[hdr->cmd](h, ctx, buf);

cleanup:
    if (cmd_handle > 0) {
        mx_handle_close(cmd_handle);
    }
    return status;
}

// Launch the main event loop
mx_status_t begin_processing(mx_handle_t acpi_root) {
    acpi_handle_ctx_t* root_context = calloc(1, sizeof(acpi_handle_ctx_t));
    if (!root_context) {
        return ERR_NO_MEMORY;
    }

    mx_status_t status = ERR_BAD_STATE;
    ACPI_STATUS acpi_status = AcpiGetHandle(NULL,
                                            (char*)"\\_SB",
                                            &root_context->ns_node);
    if (acpi_status != AE_OK) {
        status = ERR_NOT_FOUND;
        goto fail;
    }
    root_context->root_node = true;

    status = mxio_dispatcher_create(&dispatcher, dispatch);
    if (status != NO_ERROR) {
        goto fail;
    }

    status = mxio_dispatcher_add(dispatcher, acpi_root, root_context, NULL);
    if (status != NO_ERROR) {
        goto fail;
    }

    mxio_dispatcher_run(dispatcher);
    // mxio_dispatcher_run should not return
    return ERR_BAD_STATE;

fail:
    free(root_context);
    return status;
}

// Check if *buf* is a valid PNP or ACPI id.  *len* does not include a null byte
static bool is_pnp_acpi_id(char* buf, unsigned int len) {
    if (len < 7) {
        return false;
    }

    if (!memcmp(buf, "PNP", 3) && len == 7) {
        // Check if valid PNP ID
        return isxdigit(buf[3]) && isxdigit(buf[4]) &&
               isxdigit(buf[5]) && isxdigit(buf[6]);
    } else if (len == 8) {
        // Check if valid ACPI ID
        return (isupper(buf[0]) || isdigit(buf[0])) &&
               (isupper(buf[1]) || isdigit(buf[1])) &&
               (isupper(buf[2]) || isdigit(buf[2])) &&
               (isupper(buf[3]) || isdigit(buf[3])) &&
               isxdigit(buf[4]) && isxdigit(buf[5]) &&
               isxdigit(buf[6]) && isxdigit(buf[7]);
    }

    return false;
}

// Check if *name* is a valid ACPI name
static bool is_valid_name(char name[4]) {
    return (isalnum(name[0]) || name[0] == '_') &&
           (isalnum(name[1]) || name[1] == '_') &&
           (isalnum(name[2]) || name[2] == '_') &&
           (isalnum(name[3]) || name[3] == '_');
}

// Send an error response.
static mx_status_t send_error(mx_handle_t h, uint32_t req_id, mx_status_t status) {
    acpi_rsp_hdr_t rsp = {
        .status = status,
        .len = sizeof(rsp),
        .request_id = req_id,
    };

    return mx_msgpipe_write(h, &rsp, sizeof(rsp), NULL, 0, 0);
}

static mx_status_t cmd_list_children(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    mx_status_t status = NO_ERROR;

    acpi_cmd_list_children_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, ERR_INVALID_ARGS);
    }

    // Begin by finding the number of children
    uint32_t num_children = 0;
    ACPI_HANDLE child = NULL;
    while (1) {
        ACPI_STATUS acpi_status = AcpiGetNextObject(
            ACPI_TYPE_DEVICE, ctx->ns_node, child, &child);
        if (acpi_status == AE_NOT_FOUND) {
            break;
        }
        if (acpi_status != AE_OK) {
            return ERR_BAD_STATE;
        }
        num_children++;
    }

    acpi_rsp_list_children_t* rsp = NULL;

    const uint32_t rsp_size = sizeof(*rsp) + sizeof(rsp->children[0]) * num_children;
    rsp = calloc(1, rsp_size);
    if (!rsp) {
        return send_error(h, cmd->hdr.request_id, ERR_NO_MEMORY);
    }

    rsp->hdr.status = NO_ERROR;
    rsp->hdr.len = rsp_size;
    rsp->hdr.request_id = cmd->hdr.request_id,
    rsp->num_children = num_children;

    num_children = 0;
    child = NULL;
    while (num_children < rsp->num_children) {
        ACPI_STATUS acpi_status = AcpiGetNextObject(
            ACPI_TYPE_DEVICE, ctx->ns_node, child, &child);
        if (acpi_status == AE_NOT_FOUND) {
            break;
        } else if (acpi_status != AE_OK) {
            status = ERR_BAD_STATE;
            goto cleanup;
        }

        ACPI_DEVICE_INFO* info = NULL;
        acpi_status = AcpiGetObjectInfo(child, &info);
        if (acpi_status == AE_NO_MEMORY) {
            status = send_error(h, cmd->hdr.request_id, ERR_NO_MEMORY);
            goto cleanup;
        } else if (acpi_status != AE_OK) {
            status = ERR_BAD_STATE;
            goto cleanup;
        }

        // Populate name
        memcpy(rsp->children[num_children].name, &info->Name, 4);

        // Populate HID
        if (info->Valid & ACPI_VALID_HID) {
            // Add 1 since the Length values count null bytes
            if (is_pnp_acpi_id(info->HardwareId.String,
                               info->HardwareId.Length - 1)) {

                assert(info->HardwareId.Length <= sizeof(rsp->children[0].cid[0]) + 1);
                memcpy(rsp->children[num_children].hid,
                       info->HardwareId.String,
                       info->HardwareId.Length - 1);
            }
        }

        // Populate CID list
        if (info->Valid & ACPI_VALID_CID) {
            ACPI_PNP_DEVICE_ID_LIST* cid_list = &info->CompatibleIdList;

            for (uint32_t i = 0, cid_used = 0;
                 i < cid_list->Count && cid_used < countof(rsp->children[0].cid);
                 ++i) {

                if (!is_pnp_acpi_id(cid_list->Ids[i].String,
                                    cid_list->Ids[i].Length - 1)) {
                    continue;
                }

                assert(cid_list->Ids[i].Length <= sizeof(rsp->children[0].cid[0]) + 1);
                memcpy(rsp->children[num_children].cid[cid_used],
                       cid_list->Ids[i].String,
                       cid_list->Ids[i].Length - 1);

                cid_used++;
            }
        }
        ACPI_FREE(info);

        num_children++;
    }

    // Sanity check that we enumerated the same number as we started with
    if (num_children != rsp->num_children) {
        status = ERR_BAD_STATE;
        goto cleanup;
    }

    status = mx_msgpipe_write(h, rsp, rsp_size, NULL, 0, 0);

cleanup:
    free(rsp);
    return status;
}

static mx_status_t cmd_get_child_handle(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    mx_status_t status = NO_ERROR;

    acpi_cmd_get_child_handle_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd) || !is_valid_name(cmd->name)) {
        return send_error(h, cmd->hdr.request_id, ERR_INVALID_ARGS);
    }

    // Search for child
    char name[5] = {0};
    memcpy(name, cmd->name, sizeof(cmd->name));
    ACPI_HANDLE child_ns_node;
    ACPI_STATUS acpi_status = AcpiGetHandle(ctx->ns_node, name, &child_ns_node);
    if (acpi_status != AE_OK) {
        return send_error(h, cmd->hdr.request_id, ERR_NOT_FOUND);
    }

    // Build a context for the child handle
    acpi_handle_ctx_t* child_ctx = calloc(1, sizeof(*child_ctx));
    if (!child_ctx) {
        return send_error(h, cmd->hdr.request_id, ERR_NO_MEMORY);
    }
    child_ctx->ns_node = child_ns_node;
    child_ctx->root_node = false;

    mx_handle_t msg_pipe[2];
    status = mx_msgpipe_create(msg_pipe, 0);
    if (status != NO_ERROR) {
        free(child_ctx);
        return send_error(h, cmd->hdr.request_id, status);
    }

    status = mxio_dispatcher_add(dispatcher, msg_pipe[1], child_ctx, NULL);
    if (status != NO_ERROR) {
        status = send_error(h, cmd->hdr.request_id, status);
        goto cleanup;
    }

    acpi_rsp_get_child_handle_t rsp = {
        .hdr = {
            .status = NO_ERROR,
            .len = sizeof(rsp),
            .request_id = cmd->hdr.request_id,
        },
    };

    status = mx_msgpipe_write(h, &rsp, sizeof(rsp), msg_pipe, 1, 0);
    if (status != NO_ERROR) {
        goto cleanup;
    }

    return NO_ERROR;
cleanup:
    mx_handle_close(msg_pipe[0]);
    mx_handle_close(msg_pipe[1]);
    free(child_ctx);
    return status;
}

static mx_status_t cmd_get_pci_init_arg(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    mx_status_t status = NO_ERROR;

    acpi_cmd_get_pci_init_arg_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, ERR_INVALID_ARGS);
    }

    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS acpi_status = AcpiGetObjectInfo(ctx->ns_node, &info);
    if (acpi_status == AE_NO_MEMORY) {
        return send_error(h, cmd->hdr.request_id, ERR_NO_MEMORY);
    } else if (acpi_status != AE_OK) {
        return ERR_BAD_STATE;
    }

    // Make sure this is the right type of namespace node
    if (!(info->Flags & ACPI_PCI_ROOT_BRIDGE)) {
        ACPI_FREE(info);
        return send_error(h, cmd->hdr.request_id, ERR_WRONG_TYPE);
    }
    ACPI_FREE(info);

    acpi_rsp_get_pci_init_arg_t* rsp = NULL;

    mx_pci_init_arg_t* arg = NULL;
    uint32_t arg_size;
    status = get_pci_init_arg(&arg, &arg_size);
    if (status != NO_ERROR) {
        return send_error(h, cmd->hdr.request_id, status);
    }

    uint32_t len = arg_size + offsetof(acpi_rsp_get_pci_init_arg_t, arg);
    rsp = malloc(len);
    if (!rsp) {
        status = send_error(h, cmd->hdr.request_id, ERR_NO_MEMORY);
        goto cleanup;
    }

    rsp->hdr.status = NO_ERROR;
    rsp->hdr.len = len;
    rsp->hdr.request_id = cmd->hdr.request_id,
    memcpy(&rsp->arg, arg, arg_size);

    status = mx_msgpipe_write(h, rsp, len, NULL, 0, 0);

cleanup:
    free(arg);
    free(rsp);
    return status;
}

static mx_status_t cmd_s_state_transition(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    acpi_cmd_s_state_transition_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, ERR_INVALID_ARGS);
    }

    if (!ctx->root_node) {
        return send_error(h, cmd->hdr.request_id, ERR_ACCESS_DENIED);
    }

    switch (cmd->target_state) {
    case ACPI_S_STATE_REBOOT:
        reboot();
        break;
    case ACPI_S_STATE_S5:
        poweroff();
        break;
    case ACPI_S_STATE_S3: // fall-through since suspend-to-RAM is not yet supported
    default:
        return send_error(h, cmd->hdr.request_id, ERR_NOT_SUPPORTED);
    }
    return send_error(h, cmd->hdr.request_id, ERR_INTERNAL);
}

static mx_status_t cmd_ps0(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    acpi_cmd_ps0_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, ERR_INVALID_ARGS);
    }

    if (!ctx->root_node) {
        return send_error(h, cmd->hdr.request_id, ERR_ACCESS_DENIED);
    }

    cmd->name[sizeof(cmd->name) - 1] = '\0';
    ACPI_HANDLE dev;
    ACPI_STATUS status = AcpiGetHandle(NULL, cmd->name, &dev);
    if (status != AE_OK) {
        printf("Failed to find path %s\n", cmd->name);
        return send_error(h, cmd->hdr.request_id, ERR_NOT_FOUND);
    }

    status = AcpiEvaluateObject(dev, (char*)"_PS0", NULL, NULL);
    if (status != AE_OK) {
        printf("Failed to find object's PS0 method\n");
        return send_error(h, cmd->hdr.request_id, ERR_NOT_FOUND);
    }

    acpi_rsp_ps0_t rsp = {
        .hdr = {
            .status = NO_ERROR,
            .len = sizeof(rsp),
            .request_id = cmd->hdr.request_id,
        },
    };

    return mx_msgpipe_write(h, &rsp, sizeof(rsp), NULL, 0, 0);
}

static mx_status_t cmd_new_connection(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    // if a handle was passed with this, as it should be
    // this command would have been handled without calling this function
    return ERR_INVALID_ARGS;
}
