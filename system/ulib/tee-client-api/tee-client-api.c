// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <zircon/device/tee.h>

#include <tee-client-api/tee_client_api.h>

#define DEFAULT_TEE "/dev/class/tee/000"

bool is_global_platform_compliant(int fd) {
    tee_ioctl_description_t tee_description;

    ssize_t ret = ioctl_tee_get_description(fd, &tee_description);

    return ret == sizeof(tee_description) ? tee_description.is_global_platform_compliant : false;
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

    if (!is_global_platform_compliant(fd)) {
        // This API is only designed to support TEEs that are Global Platform compliant.
        close(fd);
        return TEEC_ERROR_NOT_SUPPORTED;
    }
    context->imp.fd = fd;

    return TEEC_SUCCESS;
}

void TEEC_FinalizeContext(TEEC_Context* context) {
    if (context) {
        close(context->imp.fd);
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
    return TEEC_ERROR_NOT_IMPLEMENTED;
}

void TEEC_CloseSession(TEEC_Session* session) {}

TEEC_Result TEEC_InvokeCommand(TEEC_Session* session,
                               uint32_t commandID,
                               TEEC_Operation* operation,
                               uint32_t* returnOrigin) {
    return TEEC_ERROR_NOT_IMPLEMENTED;
}

void TEEC_RequestCancellation(TEEC_Operation* operation) {}
