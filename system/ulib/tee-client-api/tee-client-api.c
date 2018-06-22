// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <tee-client-api/tee_client_api.h>

TEEC_Result TEEC_InitializeContext(const char* name, TEEC_Context* context) {
    return TEEC_ERROR_NOT_IMPLEMENTED;
}

void TEEC_FinalizeContext(TEEC_Context* context) {}

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
