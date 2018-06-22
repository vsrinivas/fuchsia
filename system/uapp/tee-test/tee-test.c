// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <stdio.h>

#include <tee-client-api/tee_client_api.h>

int main(int argc, const char** argv) {
    const char* prog_name = argv[0];

    TEEC_Result result;
    TEEC_Context context;

    result = TEEC_InitializeContext(NULL, &context);
    if (result != TEEC_SUCCESS) {
        printf("%s: Failed to initialize context (%x)\n", prog_name, result);
        return result;
    }

    /* Can't really do anything yet, so let's just close it back out. */
    TEEC_FinalizeContext(&context);

    return result;
}
