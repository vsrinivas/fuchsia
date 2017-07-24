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

// Data associated with each channel handle
typedef struct {
    // The namespace node associated with this handle.  The
    // handle should be allowed to access ACPI resources further up
    // the namespace tree.
    ACPI_HANDLE ns_node;
    bool root_node;
    mx_handle_t notify;    // event port
    uint32_t event_mask;
    uint64_t event_key;
} acpi_handle_ctx_t;

// Command functions.  These should return an error only if the connection
// should be aborted.  Otherwise they should send their own replies.
static mx_status_t cmd_list_children(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_get_child_handle(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_get_pci_init_arg(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_s_state_transition(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_ps0(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_bst(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_bif(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_enable_event(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);
static mx_status_t cmd_new_connection(mx_handle_t h, acpi_handle_ctx_t* ctx, void* cmd);

typedef mx_status_t (*cmd_handler_t)(mx_handle_t, acpi_handle_ctx_t*, void*);
static const cmd_handler_t cmd_table[] = {
        [ACPI_CMD_LIST_CHILDREN] = cmd_list_children,
        [ACPI_CMD_GET_CHILD_HANDLE] = cmd_get_child_handle,
        [ACPI_CMD_GET_PCI_INIT_ARG] = cmd_get_pci_init_arg,
        [ACPI_CMD_S_STATE_TRANSITION] = cmd_s_state_transition,
        [ACPI_CMD_PS0] = cmd_ps0,
        [ACPI_CMD_BST] = cmd_bst,
        [ACPI_CMD_BIF] = cmd_bif,
        [ACPI_CMD_ENABLE_EVENT] = cmd_enable_event,
        [ACPI_CMD_NEW_CONNECTION] = cmd_new_connection,
};

static mx_status_t send_error(mx_handle_t h, uint32_t req_id, mx_status_t status);

static inline uint32_t acpi_event_type(uint16_t events) {
    if ((events & ACPI_EVENT_SYSTEM_NOTIFY) && (events & ACPI_EVENT_DEVICE_NOTIFY)) {
        return ACPI_ALL_NOTIFY;
    } else if (events & ACPI_EVENT_SYSTEM_NOTIFY) {
        return ACPI_SYSTEM_NOTIFY;
    } else if (events & ACPI_EVENT_DEVICE_NOTIFY) {
        return ACPI_DEVICE_NOTIFY;
    } else {
        return 0;
    }
}

static void notify_handler(ACPI_HANDLE node, uint32_t value, void* _ctx) {
    acpi_handle_ctx_t* ctx = _ctx;
    if (ctx->ns_node != node) {
        return;
    }
    uint16_t type;
    if (value <= 0x7f) {
        type = ACPI_EVENT_SYSTEM_NOTIFY;
    } else if (value <= 0xff) {
        type = ACPI_EVENT_DEVICE_NOTIFY;
    } else {
        return;
    }
    acpi_event_packet_t pkt = {
        .pkt_key = ctx->event_key,
        .version = 0,
        .type = type,
        .arg = value,
    };
    mx_port_queue(ctx->notify, &pkt, 0);
}

static mxio_dispatcher_t* dispatcher;
static mx_status_t dispatch(mx_handle_t h, void* _ctx, void* cookie) {
    acpi_handle_ctx_t* ctx = _ctx;

    // Check if handle is closed
    if (h == 0) {
        if (ctx->notify != MX_HANDLE_INVALID) {
            AcpiRemoveNotifyHandler(ctx->ns_node, acpi_event_type(ctx->event_mask), notify_handler);
            mx_handle_close(ctx->notify);
        }
        free(ctx);
        return MX_OK;
    }

    uint32_t num_bytes = 0;
    uint32_t num_handles = 0;
    mx_status_t status = mx_channel_read(h, 0, NULL, NULL, 0, 0, &num_bytes, &num_handles);
    if (status == MX_ERR_BAD_STATE) {
        return ERR_DISPATCHER_NO_WORK;
    } else if (status != MX_ERR_BUFFER_TOO_SMALL ||
               num_handles > 1 ||
               num_bytes > ACPI_MAX_REQUEST_SIZE) {
        // Trigger a close on our end
        return status;
    }

    mx_handle_t cmd_handle = 0;
    uint8_t buf[ACPI_MAX_REQUEST_SIZE];
    status = mx_channel_read(h, 0, buf, &cmd_handle, num_bytes,
            num_handles, &num_bytes, &num_handles);
    if (status != MX_OK) {
        goto cleanup;
    }

    // Validate we have at least a command header
    if (num_bytes < sizeof(acpi_cmd_hdr_t)) {
        status = MX_ERR_INVALID_ARGS;
        goto cleanup;
    }

    acpi_cmd_hdr_t* hdr = (acpi_cmd_hdr_t*)buf;
    if (hdr->version != 0) {
        status = send_error(h, hdr->request_id, MX_ERR_NOT_SUPPORTED);
        goto cleanup;
    }
    if (hdr->len != num_bytes) {
        status = send_error(h, hdr->request_id, MX_ERR_INVALID_ARGS);
        goto cleanup;
    }

    // Dispatch the actual command
    if (hdr->cmd >= countof(cmd_table) || cmd_table[hdr->cmd] == NULL) {
        status = send_error(h, hdr->request_id, MX_ERR_NOT_SUPPORTED);
        goto cleanup;
    }
    if (hdr->cmd == ACPI_CMD_ENABLE_EVENT && num_handles == 0) {
        status = send_error(h, hdr->request_id, MX_ERR_INVALID_ARGS);
        goto cleanup;
    }
    if (num_handles > 0) {
        if (hdr->cmd == ACPI_CMD_NEW_CONNECTION) {
            acpi_handle_ctx_t* context = calloc(1, sizeof(acpi_handle_ctx_t));
            if (context == NULL) {
                status = MX_ERR_NO_MEMORY;
                goto cleanup;
            }
            context->root_node = ctx->root_node;
            context->ns_node = ctx->ns_node;
            if ((status = mxio_dispatcher_add(dispatcher, cmd_handle, context, NULL)) < 0) {
                free(context);
                goto cleanup;
            }
            acpi_rsp_hdr_t rsp;
            rsp.status = MX_OK;
            rsp.len = sizeof(rsp);
            rsp.request_id = hdr->request_id;
            return mx_channel_write(h, 0, &rsp, sizeof(rsp), NULL, 0);
        } else if (hdr->cmd == ACPI_CMD_ENABLE_EVENT) {
            if (ctx->notify != MX_HANDLE_INVALID) {
                status = MX_ERR_ALREADY_EXISTS;
                goto cleanup;
            }
            // Set the notify handle here because the command table doesn't accept a handle
            // in the parameter.
            ctx->notify = cmd_handle;
            // Fall through to call the command table
        } else {
            status = MX_ERR_INVALID_ARGS;
            goto cleanup;
        }
    }
    return cmd_table[hdr->cmd](h, ctx, buf);

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
        return MX_ERR_NO_MEMORY;
    }

    mx_status_t status = MX_ERR_BAD_STATE;
    ACPI_STATUS acpi_status = AcpiGetHandle(NULL,
                                            (char*)"\\_SB",
                                            &root_context->ns_node);
    if (acpi_status != AE_OK) {
        status = MX_ERR_NOT_FOUND;
        goto fail;
    }
    root_context->root_node = true;

    status = mxio_dispatcher_create(&dispatcher, dispatch);
    if (status != MX_OK) {
        goto fail;
    }

    status = mxio_dispatcher_add(dispatcher, acpi_root, root_context, NULL);
    if (status != MX_OK) {
        goto fail;
    }

    mxio_dispatcher_run(dispatcher);
    // mxio_dispatcher_run should not return
    return MX_ERR_BAD_STATE;

fail:
    free(root_context);
    return status;
}

bool is_pnp_acpi_id(char* buf, unsigned int len) {
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

    return mx_channel_write(h, 0, &rsp, sizeof(rsp), NULL, 0);
}

static mx_status_t cmd_list_children(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    mx_status_t status = MX_OK;

    acpi_cmd_list_children_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_INVALID_ARGS);
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
            return MX_ERR_BAD_STATE;
        }
        num_children++;
    }

    acpi_rsp_list_children_t* rsp = NULL;

    const uint32_t rsp_size = sizeof(*rsp) + sizeof(rsp->children[0]) * num_children;
    rsp = calloc(1, rsp_size);
    if (!rsp) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_NO_MEMORY);
    }

    rsp->hdr.status = MX_OK;
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
            status = MX_ERR_BAD_STATE;
            goto cleanup;
        }

        ACPI_DEVICE_INFO* info = NULL;
        acpi_status = AcpiGetObjectInfo(child, &info);
        if (acpi_status == AE_NO_MEMORY) {
            status = send_error(h, cmd->hdr.request_id, MX_ERR_NO_MEMORY);
            goto cleanup;
        } else if (acpi_status != AE_OK) {
            status = MX_ERR_BAD_STATE;
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
        status = MX_ERR_BAD_STATE;
        goto cleanup;
    }

    status = mx_channel_write(h, 0, rsp, rsp_size, NULL, 0);

cleanup:
    free(rsp);
    return status;
}

static mx_status_t cmd_get_child_handle(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    mx_status_t status = MX_OK;

    acpi_cmd_get_child_handle_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd) || !is_valid_name(cmd->name)) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_INVALID_ARGS);
    }

    // Search for child
    char name[5] = {0};
    memcpy(name, cmd->name, sizeof(cmd->name));
    ACPI_HANDLE child_ns_node;
    ACPI_STATUS acpi_status = AcpiGetHandle(ctx->ns_node, name, &child_ns_node);
    if (acpi_status != AE_OK) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_NOT_FOUND);
    }

    // Build a context for the child handle
    acpi_handle_ctx_t* child_ctx = calloc(1, sizeof(*child_ctx));
    if (!child_ctx) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_NO_MEMORY);
    }
    child_ctx->ns_node = child_ns_node;
    child_ctx->root_node = false;

    mx_handle_t msg_pipe[2];
    status = mx_channel_create(0, &msg_pipe[0], &msg_pipe[1]);
    if (status != MX_OK) {
        free(child_ctx);
        return send_error(h, cmd->hdr.request_id, status);
    }

    status = mxio_dispatcher_add(dispatcher, msg_pipe[1], child_ctx, NULL);
    if (status != MX_OK) {
        status = send_error(h, cmd->hdr.request_id, status);
        goto cleanup;
    }

    acpi_rsp_get_child_handle_t rsp = {
        .hdr = {
            .status = MX_OK,
            .len = sizeof(rsp),
            .request_id = cmd->hdr.request_id,
        },
    };

    status = mx_channel_write(h, 0, &rsp, sizeof(rsp), msg_pipe, 1);
    if (status != MX_OK) {
        goto cleanup;
    }

    return MX_OK;
cleanup:
    mx_handle_close(msg_pipe[0]);
    mx_handle_close(msg_pipe[1]);
    free(child_ctx);
    return status;
}

static mx_status_t cmd_get_pci_init_arg(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    mx_status_t status = MX_OK;

    acpi_cmd_get_pci_init_arg_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_INVALID_ARGS);
    }

    acpi_rsp_get_pci_init_arg_t* rsp = NULL;

    mx_pci_init_arg_t* arg = NULL;
    uint32_t arg_size;
    status = get_pci_init_arg(&arg, &arg_size);
    if (status != MX_OK) {
        return send_error(h, cmd->hdr.request_id, status);
    }

    uint32_t len = arg_size + offsetof(acpi_rsp_get_pci_init_arg_t, arg);
    rsp = malloc(len);
    if (!rsp) {
        status = send_error(h, cmd->hdr.request_id, MX_ERR_NO_MEMORY);
        goto cleanup;
    }

    rsp->hdr.status = MX_OK;
    rsp->hdr.len = len;
    rsp->hdr.request_id = cmd->hdr.request_id,
    memcpy(&rsp->arg, arg, arg_size);

    status = mx_channel_write(h, 0, rsp, len, NULL, 0);

cleanup:
    free(arg);
    free(rsp);
    return status;
}

static mx_status_t cmd_s_state_transition(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    acpi_cmd_s_state_transition_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_INVALID_ARGS);
    }

    if (!ctx->root_node) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_ACCESS_DENIED);
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
        return send_error(h, cmd->hdr.request_id, MX_ERR_NOT_SUPPORTED);
    }
    return send_error(h, cmd->hdr.request_id, MX_ERR_INTERNAL);
}

static mx_status_t cmd_ps0(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    acpi_cmd_ps0_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_INVALID_ARGS);
    }

    if (!ctx->root_node) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_ACCESS_DENIED);
    }

    cmd->name[sizeof(cmd->name) - 1] = '\0';
    ACPI_HANDLE dev;
    ACPI_STATUS status = AcpiGetHandle(NULL, cmd->name, &dev);
    if (status != AE_OK) {
        printf("Failed to find path %s\n", cmd->name);
        return send_error(h, cmd->hdr.request_id, MX_ERR_NOT_FOUND);
    }

    status = AcpiEvaluateObject(dev, (char*)"_PS0", NULL, NULL);
    if (status != AE_OK) {
        printf("Failed to find object's PS0 method\n");
        return send_error(h, cmd->hdr.request_id, MX_ERR_NOT_FOUND);
    }

    acpi_rsp_ps0_t rsp = {
        .hdr = {
            .status = MX_OK,
            .len = sizeof(rsp),
            .request_id = cmd->hdr.request_id,
        },
    };

    return mx_channel_write(h, 0, &rsp, sizeof(rsp), NULL, 0);
}

static mx_status_t cmd_bst(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    acpi_cmd_bst_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_INVALID_ARGS);
    }

    ACPI_BUFFER buffer = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    mx_status_t status = AcpiEvaluateObject(ctx->ns_node, (char*)"_BST", NULL, &buffer);
    if (status != AE_OK) {
        printf("Failed to find object's BST method\n");
        return send_error(h, cmd->hdr.request_id, MX_ERR_NOT_FOUND);
    }

    ACPI_OBJECT* obj = (ACPI_OBJECT*)buffer.Pointer;
    if (obj->Type != ACPI_TYPE_PACKAGE || obj->Package.Count != 4) {
        ACPI_FREE(obj);
        return send_error(h, cmd->hdr.request_id, MX_ERR_INTERNAL);
    }
    ACPI_OBJECT* elem = obj->Package.Elements;
    for (int i = 0; i < 4; i++) {
        if (elem[i].Type != ACPI_TYPE_INTEGER) {
            ACPI_FREE(obj);
            return send_error(h, cmd->hdr.request_id, MX_ERR_INTERNAL);
        }
    }

    acpi_rsp_bst_t rsp = {
        .hdr = {
            .status = MX_OK,
            .len = sizeof(rsp),
            .request_id = cmd->hdr.request_id,
        },
        .state = elem[0].Integer.Value,
        .rate_present = elem[1].Integer.Value,
        .capacity_remaining = elem[2].Integer.Value,
        .voltage_present = elem[3].Integer.Value,
    };
    ACPI_FREE(obj);

    return mx_channel_write(h, 0, &rsp, sizeof(rsp), NULL, 0);
}

static mx_status_t cmd_bif(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    acpi_cmd_bif_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_INVALID_ARGS);
    }

    ACPI_BUFFER buffer = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    mx_status_t status = AcpiEvaluateObject(ctx->ns_node, (char*)"_BIF", NULL, &buffer);
    if (status != AE_OK) {
        printf("Failed to find object's BIF method\n");
        return send_error(h, cmd->hdr.request_id, MX_ERR_NOT_FOUND);
    }

    ACPI_OBJECT* obj = (ACPI_OBJECT*)buffer.Pointer;
    if (obj->Type != ACPI_TYPE_PACKAGE || obj->Package.Count != 13) {
        ACPI_FREE(obj);
        return send_error(h, cmd->hdr.request_id, MX_ERR_INTERNAL);
    }
    ACPI_OBJECT* elem = obj->Package.Elements;
    for (int i = 0; i < 9; i++) {
        if (elem[i].Type != ACPI_TYPE_INTEGER) {
            ACPI_FREE(obj);
            return send_error(h, cmd->hdr.request_id, MX_ERR_INTERNAL);
        }
    }
    for (int i = 9; i < 13; i++) {
        if (elem[i].Type != ACPI_TYPE_STRING) {
            ACPI_FREE(obj);
            return send_error(h, cmd->hdr.request_id, MX_ERR_INTERNAL);
        }
    }

    acpi_rsp_bif_t rsp = {
        .hdr = {
            .status = MX_OK,
            .len = sizeof(rsp),
            .request_id = cmd->hdr.request_id,
        },
        .power_unit = elem[0].Integer.Value,
        .capacity_design = elem[1].Integer.Value,
        .capacity_full = elem[2].Integer.Value,
        .technology = elem[3].Integer.Value,
        .voltage_design = elem[4].Integer.Value,
        .capacity_warning = elem[5].Integer.Value,
        .capacity_low = elem[6].Integer.Value,
        .capacity_granularity = elem[7].Integer.Value,
        .capacity_granularity2 = elem[8].Integer.Value,
    };
    strncpy(rsp.model, elem[9].String.Pointer, sizeof(rsp.model));
    strncpy(rsp.serial, elem[10].String.Pointer, sizeof(rsp.serial));
    strncpy(rsp.type, elem[11].String.Pointer, sizeof(rsp.type));
    strncpy(rsp.oem, elem[12].String.Pointer, sizeof(rsp.oem));
    rsp.model[sizeof(rsp.model)-1] = '\0';
    rsp.serial[sizeof(rsp.serial)-1] = '\0';
    rsp.type[sizeof(rsp.type)-1] = '\0';
    rsp.oem[sizeof(rsp.oem)-1] = '\0';
    ACPI_FREE(obj);

    return mx_channel_write(h, 0, &rsp, sizeof(rsp), NULL, 0);
}

static mx_status_t cmd_enable_event(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    acpi_cmd_enable_event_t* cmd = _cmd;
    if (cmd->hdr.len != sizeof(*cmd)) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_INVALID_ARGS);
    }

    if (ctx->notify == MX_HANDLE_INVALID) {
        return send_error(h, cmd->hdr.request_id, MX_ERR_BAD_STATE);
    }

    uint32_t type;
    if ((type = acpi_event_type(cmd->type)) == 0) {
        // FIXME(yky): other ACPI event types
        return send_error(h, cmd->hdr.request_id, MX_ERR_NOT_SUPPORTED);
    }

    uint16_t old_mask = ctx->event_mask;
    uint64_t old_key = ctx->event_key;
    ctx->event_mask = cmd->type;
    ctx->event_key = cmd->key;

    ACPI_STATUS acpi_status = AcpiInstallNotifyHandler(ctx->ns_node, type, notify_handler, ctx);
    if (acpi_status != AE_OK) {
        ctx->event_mask = old_mask;
        ctx->event_key = old_key;
        return send_error(h, cmd->hdr.request_id, MX_ERR_BAD_STATE);
    }

    acpi_rsp_enable_event_t rsp = {
        .hdr = {
            .status = MX_OK,
            .len = sizeof(rsp),
            .request_id = cmd->hdr.request_id,
        },
    };
    return mx_channel_write(h, 0, &rsp, sizeof(rsp), NULL, 0);
}

static mx_status_t cmd_new_connection(mx_handle_t h, acpi_handle_ctx_t* ctx, void* _cmd) {
    // if a handle was passed with this, as it should be
    // this command would have been handled without calling this function
    return MX_ERR_INVALID_ARGS;
}
