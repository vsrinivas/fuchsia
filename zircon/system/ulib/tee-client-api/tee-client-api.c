// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <fuchsia/tee/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>

#include <tee-client-api/tee_client_api.h>

#define DEFAULT_TEE "/dev/class/tee/000"

#define GET_PARAM_TYPE_FOR_INDEX(param_types, index) \
    ((param_types >> (4 * index)) & 0xF)

static inline bool is_shared_mem_flag_inout(uint32_t flags) {
    const uint32_t inout_flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
    return (flags & inout_flags) == inout_flags;
}

static inline bool is_direction_input(fuchsia_tee_Direction direction) {
    return ((direction == fuchsia_tee_Direction_INPUT) ||
            (direction == fuchsia_tee_Direction_INOUT));
}

static inline bool is_direction_output(fuchsia_tee_Direction direction) {
    return ((direction == fuchsia_tee_Direction_OUTPUT) ||
            (direction == fuchsia_tee_Direction_INOUT));
}

static bool is_global_platform_compliant(zx_handle_t tee_channel) {
    fuchsia_tee_OsInfo os_info;
    zx_status_t status = fuchsia_tee_DeviceGetOsInfo(tee_channel, &os_info);

    return status == ZX_OK ? os_info.is_global_platform_compliant : false;
}

static void convert_teec_uuid_to_zx_uuid(const TEEC_UUID* teec_uuid,
                                         fuchsia_tee_Uuid* out_uuid) {
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

static uint32_t convert_zx_to_teec_return_origin(fuchsia_tee_ReturnOrigin return_origin) {
    switch (return_origin) {
    case fuchsia_tee_ReturnOrigin_COMMUNICATION:
        return TEEC_ORIGIN_COMMS;
    case fuchsia_tee_ReturnOrigin_TRUSTED_OS:
        return TEEC_ORIGIN_TEE;
    case fuchsia_tee_ReturnOrigin_TRUSTED_APPLICATION:
        return TEEC_ORIGIN_TRUSTED_APP;
    default:
        return TEEC_ORIGIN_API;
    }
}

static void close_all_vmos(const fuchsia_tee_ParameterSet* parameter_set) {
    ZX_DEBUG_ASSERT(parameter_set);

    for (size_t i = 0; i < parameter_set->count; i++) {
        const fuchsia_tee_Parameter* param = &parameter_set->parameters[i];
        if (param->tag == fuchsia_tee_ParameterTag_buffer) {
            zx_handle_close(param->buffer.vmo);
        }
    }
}

static void preprocess_value(uint32_t param_type, const TEEC_Value* teec_value,
                             fuchsia_tee_Parameter* out_zx_param) {
    ZX_DEBUG_ASSERT(teec_value);
    ZX_DEBUG_ASSERT(out_zx_param);

    fuchsia_tee_Direction direction = 0;
    switch (param_type) {
    case TEEC_VALUE_INPUT:
        direction = fuchsia_tee_Direction_INPUT;
        break;
    case TEEC_VALUE_OUTPUT:
        direction = fuchsia_tee_Direction_OUTPUT;
        break;
    case TEEC_VALUE_INOUT:
        direction = fuchsia_tee_Direction_INOUT;
        break;
    default:
        ZX_PANIC("Unknown param type");
    }

    out_zx_param->tag = fuchsia_tee_ParameterTag_value;
    out_zx_param->value.direction = direction;
    if (is_direction_input(direction)) {
        // The TEEC_Value type only includes two generic fields, whereas the Fuchsia TEE interface
        // supports three. The c field cannot be used by the TEE Client API.
        out_zx_param->value.a = teec_value->a;
        out_zx_param->value.b = teec_value->b;
        out_zx_param->value.c = 0;
    }
}

static TEEC_Result preprocess_temporary_memref(uint32_t param_type,
                                               const TEEC_TempMemoryReference* temp_memory_ref,
                                               fuchsia_tee_Parameter* out_zx_param) {
    ZX_DEBUG_ASSERT(temp_memory_ref);
    ZX_DEBUG_ASSERT(out_zx_param);

    fuchsia_tee_Direction direction;
    switch (param_type) {
    case TEEC_MEMREF_TEMP_INPUT:
        direction = fuchsia_tee_Direction_INPUT;
        break;
    case TEEC_MEMREF_TEMP_OUTPUT:
        direction = fuchsia_tee_Direction_OUTPUT;
        break;
    case TEEC_MEMREF_TEMP_INOUT:
        direction = fuchsia_tee_Direction_INOUT;
        break;
    default:
        ZX_PANIC("TEE Client API Unknown parameter type\n");
    }

    zx_handle_t vmo;

    if (!temp_memory_ref->buffer) {
        // A null buffer marked as output is a valid request to determine the necessary size of the
        // output buffer. It is an error for any sort of input.
        if (is_direction_input(direction)) {
            return TEEC_ERROR_BAD_PARAMETERS;
        }
        vmo = ZX_HANDLE_INVALID;
    } else {
        // We either have data to input or have a buffer to output data to, so create a VMO for it.
        zx_status_t status = zx_vmo_create(temp_memory_ref->size, 0, &vmo);
        if (status != ZX_OK) {
            return convert_status_to_result(status);
        }

        // If the memory reference is used as an input, then we must copy the data from the user
        // provided buffer into the VMO. There is no need to do this for parameters that are output
        // only.
        if (is_direction_input(direction)) {
            status = zx_vmo_write(vmo, temp_memory_ref->buffer, 0, temp_memory_ref->size);
            if (status != ZX_OK) {
                zx_handle_close(vmo);
                return convert_status_to_result(status);
            }
        }
    }

    out_zx_param->tag = fuchsia_tee_ParameterTag_buffer;
    out_zx_param->buffer.direction = direction;
    out_zx_param->buffer.vmo = vmo;
    out_zx_param->buffer.offset = 0;
    out_zx_param->buffer.size = temp_memory_ref->size;
    return TEEC_SUCCESS;
}

static TEEC_Result preprocess_whole_memref(const TEEC_RegisteredMemoryReference* memory_ref,
                                           fuchsia_tee_Parameter* out_zx_param) {
    ZX_DEBUG_ASSERT(memory_ref);
    ZX_DEBUG_ASSERT(out_zx_param);

    if (!memory_ref->parent) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    TEEC_SharedMemory* shared_mem = memory_ref->parent;
    fuchsia_tee_Direction direction;
    if (is_shared_mem_flag_inout(shared_mem->flags)) {
        direction = fuchsia_tee_Direction_INOUT;
    } else if (shared_mem->flags & TEEC_MEM_INPUT) {
        direction = fuchsia_tee_Direction_INPUT;
    } else if (shared_mem->flags & TEEC_MEM_OUTPUT) {
        direction = fuchsia_tee_Direction_OUTPUT;
    } else {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    zx_handle_t vmo;
    zx_status_t status = zx_handle_duplicate(shared_mem->imp.vmo, ZX_RIGHT_SAME_RIGHTS, &vmo);
    if (status != ZX_OK) {
        return convert_status_to_result(status);
    }

    out_zx_param->tag = fuchsia_tee_ParameterTag_buffer;
    out_zx_param->buffer.direction = direction;
    out_zx_param->buffer.vmo = vmo;
    out_zx_param->buffer.offset = 0;
    out_zx_param->buffer.size = shared_mem->size;

    return TEEC_SUCCESS;
}

static TEEC_Result preprocess_partial_memref(uint32_t param_type,
                                             const TEEC_RegisteredMemoryReference* memory_ref,
                                             fuchsia_tee_Parameter* out_zx_param) {
    ZX_DEBUG_ASSERT(memory_ref);
    ZX_DEBUG_ASSERT(out_zx_param);

    if (!memory_ref->parent) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    uint32_t expected_shm_flags = 0;
    fuchsia_tee_Direction direction = 0;
    switch (param_type) {
    case TEEC_MEMREF_PARTIAL_INPUT:
        expected_shm_flags = TEEC_MEM_INPUT;
        direction = fuchsia_tee_Direction_INPUT;
        break;
    case TEEC_MEMREF_PARTIAL_OUTPUT:
        expected_shm_flags = TEEC_MEM_OUTPUT;
        direction = fuchsia_tee_Direction_OUTPUT;
        break;
    case TEEC_MEMREF_PARTIAL_INOUT:
        expected_shm_flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
        direction = fuchsia_tee_Direction_INOUT;
        break;
    default:
        ZX_DEBUG_ASSERT(param_type == TEEC_MEMREF_PARTIAL_INPUT ||
                        param_type == TEEC_MEMREF_PARTIAL_OUTPUT ||
                        param_type == TEEC_MEMREF_PARTIAL_INOUT);
    }

    TEEC_SharedMemory* shared_mem = memory_ref->parent;

    if ((shared_mem->flags & expected_shm_flags) != expected_shm_flags) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    zx_handle_t vmo;
    zx_status_t status = zx_handle_duplicate(shared_mem->imp.vmo, ZX_RIGHT_SAME_RIGHTS, &vmo);
    if (status != ZX_OK) {
        return convert_status_to_result(status);
    }

    out_zx_param->tag = fuchsia_tee_ParameterTag_buffer;
    out_zx_param->buffer.direction = direction;
    out_zx_param->buffer.vmo = vmo;
    out_zx_param->buffer.offset = memory_ref->offset;
    out_zx_param->buffer.size = memory_ref->size;

    return TEEC_SUCCESS;
}

static TEEC_Result preprocess_operation(const TEEC_Operation* operation,
                                        fuchsia_tee_ParameterSet* out_parameter_set) {
    if (!operation) {
        return TEEC_SUCCESS;
    }

    TEEC_Result rc = TEEC_SUCCESS;
    for (size_t i = 0; i < TEEC_NUM_PARAMS_MAX; i++) {
        uint32_t param_type = GET_PARAM_TYPE_FOR_INDEX(operation->paramTypes, i);

        switch (param_type) {
        case TEEC_NONE:
            out_parameter_set->parameters[i].tag = fuchsia_tee_ParameterTag_empty;
            break;
        case TEEC_VALUE_INPUT:
        case TEEC_VALUE_OUTPUT:
        case TEEC_VALUE_INOUT:
            preprocess_value(param_type, &operation->params[i].value,
                             &out_parameter_set->parameters[i]);
            break;
        case TEEC_MEMREF_TEMP_INPUT:
        case TEEC_MEMREF_TEMP_OUTPUT:
        case TEEC_MEMREF_TEMP_INOUT:
            rc = preprocess_temporary_memref(param_type, &operation->params[i].tmpref,
                                             &out_parameter_set->parameters[i]);
            break;
        case TEEC_MEMREF_WHOLE:
            rc = preprocess_whole_memref(&operation->params[i].memref,
                                         &out_parameter_set->parameters[i]);
            break;
        case TEEC_MEMREF_PARTIAL_INPUT:
        case TEEC_MEMREF_PARTIAL_OUTPUT:
        case TEEC_MEMREF_PARTIAL_INOUT:
            rc = preprocess_partial_memref(param_type, &operation->params[i].memref,
                                           &out_parameter_set->parameters[i]);
            break;
        default:
            rc = TEEC_ERROR_BAD_PARAMETERS;
            break;
        }

        if (rc != TEEC_SUCCESS) {
            // Close out any VMOs we already opened for the parameters we did parse
            close_all_vmos(out_parameter_set);
            return rc;
        }
    }

    out_parameter_set->count = TEEC_NUM_PARAMS_MAX;

    return rc;
}

static TEEC_Result postprocess_value(uint32_t param_type,
                                     const fuchsia_tee_Parameter* zx_param,
                                     TEEC_Value* out_teec_value) {
    ZX_DEBUG_ASSERT(zx_param);
    ZX_DEBUG_ASSERT(out_teec_value);
    ZX_DEBUG_ASSERT(param_type == TEEC_VALUE_INPUT ||
                    param_type == TEEC_VALUE_OUTPUT ||
                    param_type == TEEC_VALUE_INOUT);

    if (zx_param->tag != fuchsia_tee_ParameterTag_value) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    const fuchsia_tee_Value* zx_value = &zx_param->value;

    // Validate that the direction of the returned parameter matches the expected.
    if ((param_type == TEEC_VALUE_INPUT) &&
        (zx_value->direction != fuchsia_tee_Direction_INPUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if ((param_type == TEEC_VALUE_OUTPUT) &&
        (zx_value->direction != fuchsia_tee_Direction_OUTPUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if ((param_type == TEEC_VALUE_INOUT) &&
        (zx_value->direction != fuchsia_tee_Direction_INOUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    // The TEEC_Value type only includes two generic fields, whereas the Fuchsia TEE interface
    // supports three. The c field cannot be used by the TEE Client API.
    out_teec_value->a = zx_value->a;
    out_teec_value->b = zx_value->b;
    return TEEC_SUCCESS;
}

static TEEC_Result postprocess_temporary_memref(uint32_t param_type,
                                                const fuchsia_tee_Parameter* zx_param,
                                                TEEC_TempMemoryReference* out_temp_memory_ref) {
    ZX_DEBUG_ASSERT(zx_param);
    ZX_DEBUG_ASSERT(out_temp_memory_ref);
    ZX_DEBUG_ASSERT(param_type == TEEC_MEMREF_TEMP_INPUT ||
                    param_type == TEEC_MEMREF_TEMP_OUTPUT ||
                    param_type == TEEC_MEMREF_TEMP_INOUT);

    if (zx_param->tag != fuchsia_tee_ParameterTag_buffer) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    const fuchsia_tee_Buffer* zx_buffer = &zx_param->buffer;

    if ((param_type == TEEC_MEMREF_TEMP_INPUT) &&
        (zx_buffer->direction != fuchsia_tee_Direction_INPUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if ((param_type == TEEC_MEMREF_TEMP_OUTPUT) &&
        (zx_buffer->direction != fuchsia_tee_Direction_OUTPUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if ((param_type == TEEC_MEMREF_TEMP_INOUT) &&
        (zx_buffer->direction != fuchsia_tee_Direction_INOUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    TEEC_Result rc = TEEC_SUCCESS;
    if (is_direction_output(zx_buffer->direction)) {
        // For output buffers, if we don't have enough space in the temporary memory reference to
        // copy the data out, we still need to update the size to indicate to the user how large of
        // a buffer they need to perform the requested operation.
        if (out_temp_memory_ref->buffer && out_temp_memory_ref->size >= zx_buffer->size) {
            zx_status_t status = zx_vmo_read(zx_buffer->vmo,
                                             out_temp_memory_ref->buffer,
                                             zx_buffer->offset,
                                             zx_buffer->size);
            rc = convert_status_to_result(status);
        }
        out_temp_memory_ref->size = zx_buffer->size;
    }

    return rc;
}

static TEEC_Result postprocess_whole_memref(const fuchsia_tee_Parameter* zx_param,
                                            TEEC_RegisteredMemoryReference* out_memory_ref) {
    ZX_DEBUG_ASSERT(zx_param);
    ZX_DEBUG_ASSERT(out_memory_ref);
    ZX_DEBUG_ASSERT(out_memory_ref->parent);

    if (zx_param->tag != fuchsia_tee_ParameterTag_buffer) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    const fuchsia_tee_Buffer* zx_buffer = &zx_param->buffer;

    if (is_direction_output(zx_buffer->direction)) {
        out_memory_ref->size = zx_buffer->size;
    }

    return TEEC_SUCCESS;
}

static TEEC_Result postprocess_partial_memref(uint32_t param_type,
                                              const fuchsia_tee_Parameter* zx_param,
                                              TEEC_RegisteredMemoryReference* out_memory_ref) {
    ZX_DEBUG_ASSERT(zx_param);
    ZX_DEBUG_ASSERT(out_memory_ref);
    ZX_DEBUG_ASSERT(param_type == TEEC_MEMREF_PARTIAL_INPUT ||
                    param_type == TEEC_MEMREF_PARTIAL_OUTPUT ||
                    param_type == TEEC_MEMREF_PARTIAL_INOUT);

    if (zx_param->tag != fuchsia_tee_ParameterTag_buffer) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    const fuchsia_tee_Buffer* zx_buffer = &zx_param->buffer;

    if ((param_type == TEEC_MEMREF_PARTIAL_INPUT) &&
        (zx_buffer->direction != fuchsia_tee_Direction_INPUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if ((param_type == TEEC_MEMREF_PARTIAL_OUTPUT) &&
        (zx_buffer->direction != fuchsia_tee_Direction_OUTPUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if ((param_type == TEEC_MEMREF_PARTIAL_INOUT) &&
        (zx_buffer->direction != fuchsia_tee_Direction_INOUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    if (is_direction_output(zx_buffer->direction)) {
        out_memory_ref->size = zx_buffer->size;
    }

    return TEEC_SUCCESS;
}

static TEEC_Result postprocess_operation(const fuchsia_tee_ParameterSet* parameter_set,
                                         TEEC_Operation* out_operation) {

    if (!out_operation) {
        return TEEC_SUCCESS;
    }

    TEEC_Result rc = TEEC_SUCCESS;
    for (size_t i = 0; i < TEEC_NUM_PARAMS_MAX; i++) {
        uint32_t param_type = GET_PARAM_TYPE_FOR_INDEX(out_operation->paramTypes, i);

        // This check catches the case where we did not receive all the parameters back that we
        // expected. Once in_param_index hits the parameter_set count, we've parsed all the
        // parameters that came back.
        if (i >= parameter_set->count) {
            rc = TEEC_ERROR_BAD_PARAMETERS;
            break;
        }

        switch (param_type) {
        case TEEC_NONE:
            if (parameter_set->parameters[i].tag != fuchsia_tee_ParameterTag_empty) {
                rc = TEEC_ERROR_BAD_PARAMETERS;
            }
            break;
        case TEEC_VALUE_INPUT:
        case TEEC_VALUE_OUTPUT:
        case TEEC_VALUE_INOUT:
            rc = postprocess_value(param_type, &parameter_set->parameters[i],
                                   &out_operation->params[i].value);
            break;
        case TEEC_MEMREF_TEMP_INPUT:
        case TEEC_MEMREF_TEMP_OUTPUT:
        case TEEC_MEMREF_TEMP_INOUT:
            rc = postprocess_temporary_memref(param_type, &parameter_set->parameters[i],
                                              &out_operation->params[i].tmpref);
            break;
        case TEEC_MEMREF_WHOLE:
            rc = postprocess_whole_memref(&parameter_set->parameters[i],
                                          &out_operation->params[i].memref);
            break;
        case TEEC_MEMREF_PARTIAL_INPUT:
        case TEEC_MEMREF_PARTIAL_OUTPUT:
        case TEEC_MEMREF_PARTIAL_INOUT:
            rc = postprocess_partial_memref(param_type, &parameter_set->parameters[i],
                                            &out_operation->params[i].memref);
            break;
        default:
            rc = TEEC_ERROR_BAD_PARAMETERS;
        }

        if (rc != TEEC_SUCCESS) {
            break;
        }
    }

    close_all_vmos(parameter_set);

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
    /* This function is supposed to register an existing buffer for use as shared memory. We don't
     * have a way of discovering the VMO handle for an arbitrary address, so implementing this would
     * require an extra VMO that would be copied into at invocation. Since we currently don't have
     * any use cases for this function and TEEC_AllocateSharedMemory should be the preferred method
     * of acquiring shared memory, we're going to leave this unimplemented for now. */
    return TEEC_ERROR_NOT_IMPLEMENTED;
}

TEEC_Result TEEC_AllocateSharedMemory(TEEC_Context* context, TEEC_SharedMemory* sharedMem) {
    if (!context || !sharedMem) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    if (sharedMem->flags & ~(TEEC_MEM_INPUT | TEEC_MEM_OUTPUT)) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    memset(&sharedMem->imp, 0, sizeof(sharedMem->imp));

    size_t size = sharedMem->size;

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    zx_status_t status = zx_vmo_create(size, ZX_VMO_NON_RESIZABLE, &vmo);
    if (status != ZX_OK) {
        return convert_status_to_result(status);
    }

    zx_vaddr_t mapped_addr;
    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
                         &mapped_addr);
    if (status != ZX_OK) {
        zx_handle_close(vmo);
        return convert_status_to_result(status);
    }

    sharedMem->buffer = (void*)mapped_addr;
    sharedMem->imp.vmo = vmo;
    sharedMem->imp.mapped_addr = mapped_addr;
    sharedMem->imp.mapped_size = size;

    return TEEC_SUCCESS;
}

void TEEC_ReleaseSharedMemory(TEEC_SharedMemory* sharedMem) {
    if (!sharedMem) {
        return;
    }
    zx_vmar_unmap(zx_vmar_root_self(), sharedMem->imp.mapped_addr, sharedMem->imp.mapped_size);
    zx_handle_close(sharedMem->imp.vmo);
}

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

    fuchsia_tee_Uuid trusted_app;
    convert_teec_uuid_to_zx_uuid(destination, &trusted_app);

    fuchsia_tee_ParameterSet parameter_set;
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
    fuchsia_tee_OpResult out_result;
    memset(&out_result, 0, sizeof(out_result));

    zx_status_t status = fuchsia_tee_DeviceOpenSession(
        context->imp.tee_channel, &trusted_app, &parameter_set, &out_session_id, &out_result);

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
    fuchsia_tee_DeviceCloseSession(session->imp.context_imp->tee_channel,
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

    fuchsia_tee_ParameterSet parameter_set;
    memset(&parameter_set, 0, sizeof(parameter_set));

    fuchsia_tee_OpResult out_result;
    memset(&out_result, 0, sizeof(out_result));

    uint32_t teec_rc = preprocess_operation(operation, &parameter_set);
    if (teec_rc != TEEC_SUCCESS) {
        if (returnOrigin) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        return teec_rc;
    }

    zx_status_t status = fuchsia_tee_DeviceInvokeCommand(
        session->imp.context_imp->tee_channel, session->imp.session_id, commandID, &parameter_set,
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
