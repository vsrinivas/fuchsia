// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <stdio.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <tee-client-api/tee_client_api.h>

const TEEC_UUID hello_world_ta = {0x8aaaf200,
                                  0x2450,
                                  0x11e4,
                                  {0xab, 0xe2, 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};

int main(int argc, const char** argv) {
    const char* prog_name = argv[0];

    TEEC_Result result;
    TEEC_Context context;
    TEEC_Session session;
    uint32_t return_origin;

    result = TEEC_InitializeContext(NULL, &context);
    if (result != TEEC_SUCCESS) {
        printf("%s: Failed to initialize context (%x)\n", prog_name, result);
        return result;
    }

    result = TEEC_OpenSession(&context, &session, &hello_world_ta,
                              TEEC_LOGIN_PUBLIC, NULL, NULL, &return_origin);
    if (result != TEEC_SUCCESS) {
        printf("%s: Failed to open session (%x %x)\n", prog_name, result, return_origin);
    }

    /* Can't really do anything yet, so let's just close it back out. */
    TEEC_CloseSession(&session);

    TEEC_FinalizeContext(&context);

    return result;
}
