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

#define GET_PARAM_TYPE_FOR_INDEX(param_types, index) \
    ((param_types >> (4 * index)) & 0xF)

static inline bool is_direction_input(zircon_tee_Direction direction) {
    return ((direction == zircon_tee_Direction_INPUT) ||
            (direction == zircon_tee_Direction_INOUT));
}

static inline bool is_direction_output(zircon_tee_Direction direction) {
    return ((direction == zircon_tee_Direction_OUTPUT) ||
            (direction == zircon_tee_Direction_INOUT));
}

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
    case ZX_OK:
        return TEEC_SUCCESS;
    }
    return TEEC_ERROR_GENERIC;
}

static uint32_t convert_zx_to_teec_return_origin(zircon_tee_ReturnOrigin return_origin) {
    switch (return_origin) {
    case zircon_tee_ReturnOrigin_COMMUNICATION:
        return TEEC_ORIGIN_COMMS;
    case zircon_tee_ReturnOrigin_TRUSTED_OS:
        return TEEC_ORIGIN_TEE;
    case zircon_tee_ReturnOrigin_TRUSTED_APPLICATION:
        return TEEC_ORIGIN_TRUSTED_APP;
    default:
        return TEEC_ORIGIN_API;
    }
}

static void preprocess_value(uint32_t param_type,
                             const TEEC_Value* teec_value,
                             zircon_tee_Parameter* out_zx_param) {
    ZX_DEBUG_ASSERT(teec_value);
    ZX_DEBUG_ASSERT(out_zx_param);

    zircon_tee_Direction direction = 0;
    switch (param_type) {
    case TEEC_VALUE_INPUT:
        direction = zircon_tee_Direction_INPUT;
        break;
    case TEEC_VALUE_OUTPUT:
        direction = zircon_tee_Direction_OUTPUT;
        break;
    case TEEC_VALUE_INOUT:
        direction = zircon_tee_Direction_INOUT;
        break;
    default:
        ZX_PANIC("Unknown param type");
    }

    out_zx_param->tag = zircon_tee_ParameterTag_value;
    out_zx_param->value.direction = direction;
    if (is_direction_input(direction)) {
        // The TEEC_Value type only includes two generic fields, whereas the Fuchsia TEE interface
        // supports three. The c field cannot be used by the TEE Client API.
        out_zx_param->value.a = teec_value->a;
        out_zx_param->value.b = teec_value->b;
        out_zx_param->value.c = 0;
    }
}

static TEEC_Result preprocess_operation(const TEEC_Operation* operation,
                                        zircon_tee_ParameterSet* out_parameter_set) {
    if (!operation) {
        return TEEC_SUCCESS;
    }

    TEEC_Result rc = TEEC_SUCCESS;
    size_t out_param_index = 0;
    for (size_t i = 0; i < TEEC_NUM_PARAMS_MAX; i++) {
        uint32_t param_type = GET_PARAM_TYPE_FOR_INDEX(operation->paramTypes, i);

        switch (param_type) {
        case TEEC_NONE:
            break;
        case TEEC_VALUE_INPUT:
        case TEEC_VALUE_OUTPUT:
        case TEEC_VALUE_INOUT:
            preprocess_value(param_type, &operation->params[i].value,
                             &out_parameter_set->parameters[out_param_index]);
            out_param_index++;
            break;
        case TEEC_MEMREF_TEMP_INPUT:
        case TEEC_MEMREF_TEMP_OUTPUT:
        case TEEC_MEMREF_TEMP_INOUT:
        case TEEC_MEMREF_WHOLE:
        case TEEC_MEMREF_PARTIAL_INPUT:
        case TEEC_MEMREF_PARTIAL_OUTPUT:
        case TEEC_MEMREF_PARTIAL_INOUT:
            return TEEC_ERROR_NOT_IMPLEMENTED;
        default:
            return TEEC_ERROR_BAD_PARAMETERS;
        }

        if (rc != TEEC_SUCCESS) {
            return rc;
        }
    }
    out_parameter_set->count = out_param_index;

    return TEEC_SUCCESS;
}

static TEEC_Result postprocess_value(uint32_t param_type,
                                     const zircon_tee_Parameter* zx_param,
                                     TEEC_Value* out_teec_value) {
    ZX_DEBUG_ASSERT(zx_param);
    ZX_DEBUG_ASSERT(out_teec_value);
    ZX_DEBUG_ASSERT(param_type == TEEC_VALUE_INPUT ||
                    param_type == TEEC_VALUE_OUTPUT ||
                    param_type == TEEC_VALUE_INOUT);

    if (zx_param->tag != zircon_tee_ParameterTag_value) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    const zircon_tee_Value* zx_value = &zx_param->value;

    // Validate that the direction of the returned parameter matches the expected.
    if ((param_type == TEEC_VALUE_INPUT) && (zx_value->direction != zircon_tee_Direction_INPUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if ((param_type == TEEC_VALUE_OUTPUT) && (zx_value->direction != zircon_tee_Direction_OUTPUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if ((param_type == TEEC_VALUE_INOUT) && (zx_value->direction != zircon_tee_Direction_INOUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    // The TEEC_Value type only includes two generic fields, whereas the Fuchsia TEE interface
    // supports three. The c field cannot be used by the TEE Client API.
    out_teec_value->a = zx_value->a;
    out_teec_value->b = zx_value->b;
    return TEEC_SUCCESS;
}

static TEEC_Result postprocess_operation(const zircon_tee_ParameterSet* parameter_set,
                                         TEEC_Operation* out_operation) {

    if (!out_operation) {
        return TEEC_SUCCESS;
    }

    TEEC_Result rc = TEEC_SUCCESS;
    size_t in_param_index = 0;
    for (size_t i = 0; i < TEEC_NUM_PARAMS_MAX; i++) {
        uint32_t param_type = GET_PARAM_TYPE_FOR_INDEX(out_operation->paramTypes, i);

        // This check catches the case where we did not receive all the parameters back that we
        // expected. Once in_param_index hits the parameter_set count, we've parsed all the
        // parameters that came back and we should just hit NONEs for the rest of the parameters in
        // the operation.
        if ((param_type != TEEC_NONE) && (in_param_index >= parameter_set->count)) {
            return TEEC_ERROR_BAD_PARAMETERS;
        }

        switch (param_type) {
        case TEEC_NONE:
            // We don't pass NONEs to the device, so skip these.
            break;
        case TEEC_VALUE_INPUT:
        case TEEC_VALUE_OUTPUT:
        case TEEC_VALUE_INOUT:
            rc = postprocess_value(param_type, &parameter_set->parameters[in_param_index],
                                   &out_operation->params[i].value);
            in_param_index++;
            break;
        case TEEC_MEMREF_TEMP_INPUT:
        case TEEC_MEMREF_TEMP_OUTPUT:
        case TEEC_MEMREF_TEMP_INOUT:
        case TEEC_MEMREF_WHOLE:
        case TEEC_MEMREF_PARTIAL_INPUT:
        case TEEC_MEMREF_PARTIAL_OUTPUT:
        case TEEC_MEMREF_PARTIAL_INOUT:
            return TEEC_ERROR_NOT_IMPLEMENTED;
        default:
            return TEEC_ERROR_BAD_PARAMETERS;
        }

        if (rc != TEEC_SUCCESS) {
            return rc;
        }
    }

    return rc;
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

    zircon_tee_Uuid trusted_app;
    convert_teec_uuid_to_zx_uuid(destination, &trusted_app);

    zircon_tee_ParameterSet parameter_set;
    memset(&parameter_set, 0, sizeof(parameter_set));

    uint32_t teec_rc = preprocess_operation(operation, &parameter_set);
    if (teec_rc != TEEC_SUCCESS) {
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        return teec_rc;
    }

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

    teec_rc = postprocess_operation(&out_result.parameter_set, operation);
    if (teec_rc != TEEC_SUCCESS) {
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        return teec_rc;
    }

    if (out_result.return_code == TEEC_SUCCESS) {
        session->imp.session_id = out_session_id;
        session->imp.context_imp = &context->imp;
    }

    if (returnOrigin) {
        *returnOrigin = convert_zx_to_teec_return_origin(out_result.return_origin);
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

    uint32_t teec_rc = preprocess_operation(operation, &parameter_set);
    if (teec_rc != TEEC_SUCCESS) {
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        return teec_rc;
    }

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

    teec_rc = postprocess_operation(&out_result.parameter_set, operation);
    if (teec_rc != TEEC_SUCCESS) {
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        return teec_rc;
    }

    if (returnOrigin) {
        *returnOrigin = convert_zx_to_teec_return_origin(out_result.return_origin);
    }

    return out_result.return_code;
}

void TEEC_RequestCancellation(TEEC_Operation* operation) {}
