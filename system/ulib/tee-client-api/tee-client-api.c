// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <lib/fdio/util.h>
#include <zircon/tee/c/fidl.h>

#include <tee-client-api/tee_client_api.h>

#define DEFAULT_TEE "/dev/class/tee/000"

static bool is_global_platform_compliant(zx_handle_t tee_channel) {
    zircon_tee_OsInfo os_info;
    zx_status_t status = zircon_tee_DeviceGetOsInfo(tee_channel, &os_info);

    return status == ZX_OK ? os_info.is_global_platform_compliant : false;
}

static void convert_teec_uuid_to_zx_uuid(const TEEC_UUID* teec_uuid,
                                         zircon_tee_Uuid* out_uuid) {
    ZX_DEBUG_ASSERT(teec_uuid);
    ZX_DEBUG_ASSERT(out_uuid);
    out_uuid->time_low = teec_uuid->timeLow;
    out_uuid->time_mid = teec_uuid->timeMid;
    out_uuid->time_hi_and_version = teec_uuid->timeHiAndVersion;
    memcpy(out_uuid->clock_seq_and_node, teec_uuid->clockSeqAndNode,
           sizeof(out_uuid->clock_seq_and_node));
}

static TEEC_Result convert_status_to_result(zx_status_t status) {
    switch (status) {
    case ZX_ERR_PEER_CLOSED:
        return TEEC_ERROR_COMMUNICATION;
    case ZX_ERR_INVALID_ARGS:
        return TEEC_ERROR_BAD_PARAMETERS;
    case ZX_ERR_NOT_SUPPORTED:
        return TEEC_ERROR_NOT_SUPPORTED;
    case ZX_ERR_NO_MEMORY:
        return TEEC_ERROR_OUT_OF_MEMORY;
    }
    return TEEC_ERROR_GENERIC;
}

TEEC_Result TEEC_InitializeContext(const char* name, TEEC_Context* context) {

    if (!context) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    const char* tee_device = (name != NULL) ? name : DEFAULT_TEE;

    int fd = open(tee_device, O_RDWR);
    if (fd < 0) {
        return TEEC_ERROR_ITEM_NOT_FOUND;
    }

    zx_handle_t tee_channel;
    zx_status_t status = fdio_get_service_handle(fd, &tee_channel);
    // Irregardless of the success or failure of fdio_get_service_handle, the original file
    // descriptor is effectively closed.
    if (status != ZX_OK) {
        return TEEC_ERROR_COMMUNICATION;
    }

    if (!is_global_platform_compliant(tee_channel)) {
        // This API is only designed to support TEEs that are Global Platform compliant.
        zx_handle_close(tee_channel);
        return TEEC_ERROR_NOT_SUPPORTED;
    }
    context->imp.tee_channel = tee_channel;

    return TEEC_SUCCESS;
}

void TEEC_FinalizeContext(TEEC_Context* context) {
    if (context) {
        zx_handle_close(context->imp.tee_channel);
    }
}

TEEC_Result TEEC_RegisterSharedMemory(TEEC_Context* context, TEEC_SharedMemory* sharedMem) {
    return TEEC_ERROR_NOT_IMPLEMENTED;
}

TEEC_Result TEEC_AllocateSharedMemory(TEEC_Context* context, TEEC_SharedMemory* sharedMem) {
    return TEEC_ERROR_NOT_IMPLEMENTED;
}

void TEEC_ReleaseSharedMemory(TEEC_SharedMemory* sharedMem) {}

TEEC_Result TEEC_OpenSession(TEEC_Context* context,
                             TEEC_Session* session,
                             const TEEC_UUID* destination,
                             uint32_t connectionMethod,
                             const void* connectionData,
                             TEEC_Operation* operation,
                             uint32_t* returnOrigin) {
    if (!context || !session || !destination) {
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_API;
        }
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    if (connectionMethod != TEEC_LOGIN_PUBLIC) {
        // TODO(rjascani): Investigate whether non public login is needed.
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_API;
        }
        return TEEC_ERROR_NOT_IMPLEMENTED;
    }

    if (operation) {
        // TODO(rjascani): Remove this once operation is handled
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_API;
        }
        return TEEC_ERROR_NOT_IMPLEMENTED;
    }

    zircon_tee_Uuid trusted_app;
    convert_teec_uuid_to_zx_uuid(destination, &trusted_app);

    zircon_tee_ParameterSet parameter_set;
    memset(&parameter_set, 0, sizeof(parameter_set));

    // Outputs
    uint32_t out_session_id;
    zircon_tee_Result out_result;
    memset(&out_result, 0, sizeof(out_result));

    zx_status_t status = zircon_tee_DeviceOpenSession(context->imp.tee_channel,
                                                      &trusted_app,
                                                      &parameter_set,
                                                      &out_session_id,
                                                      &out_result);

    if (status != ZX_OK) {
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        return convert_status_to_result(status);
    }

    if (out_result.return_code == TEEC_SUCCESS) {
        session->imp.session_id = out_session_id;
        session->imp.context_imp = &context->imp;
    }

    if (returnOrigin) {
        *returnOrigin = out_result.return_origin;
    }

    return out_result.return_code;
}

void TEEC_CloseSession(TEEC_Session* session) {
    if (!session || !session->imp.context_imp) {
        return;
    }

    // TEEC_CloseSession simply swallows errors, so no need to check here.
    zircon_tee_DeviceCloseSession(session->imp.context_imp->tee_channel,
                                  session->imp.session_id);
    session->imp.context_imp = NULL;
}

TEEC_Result TEEC_InvokeCommand(TEEC_Session* session,
                               uint32_t commandID,
                               TEEC_Operation* operation,
                               uint32_t* returnOrigin) {
    if (!session || !session->imp.context_imp) {
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_API;
        }
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    zircon_tee_ParameterSet parameter_set;
    memset(&parameter_set, 0, sizeof(parameter_set));

    zircon_tee_Result out_result;
    memset(&out_result, 0, sizeof(out_result));

    zx_status_t status = zircon_tee_DeviceInvokeCommand(session->imp.context_imp->tee_channel,
                                                        session->imp.session_id,
                                                        commandID,
                                                        &parameter_set,
                                                        &out_result);
    if (status != ZX_OK) {
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        return convert_status_to_result(status);
    }

    if (returnOrigin) {
        *returnOrigin = out_result.return_origin;
    }

    return out_result.return_code;
}

void TEEC_RequestCancellation(TEEC_Operation* operation) {}
