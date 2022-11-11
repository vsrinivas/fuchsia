// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_FUCHSIA_TCTI_INCLUDE_FUCHSIA_TCTI_H_
#define SRC_SECURITY_LIB_FUCHSIA_TCTI_INCLUDE_FUCHSIA_TCTI_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// This library has to be depended on by the TSS 2.0 interface written in plain C.
// So we must provide a C compatible interface while internally holding C++ types.
typedef void opaque_ctx_t;

// Initializes the Fuchsia TCTI interface by binding to "/dev/class/tpm" and returning
// a FuchsiaInternalContext as an opaque_ctx.
// The ownership of opaque_ctx_t is transferred to the caller.
opaque_ctx_t* fuchsia_tpm_init(void);

// Unwraps the opaque_ctx into a FuchsiaInternalContext and then calls the TPM driver FIDL
// interface to send a command to the driver.
int fuchsia_tpm_send(opaque_ctx_t* opaque_ctx, int command_code, const uint8_t* in_buffer,
                     size_t len);

// Unwraps the opaque_cxt into a FuchshiaInternalContext and then attempts to extract the
// requested number of bytes defined by len into the out_buffer. This function does not
// perform a FIDL request but instead reads from its own recv_buffer stored in the context
// that is appended to from the results of fuchsia_tpm_send.
size_t fuchsia_tpm_recv(opaque_ctx_t* opaque_ctx, uint8_t* out_buffer, size_t len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SRC_SECURITY_LIB_FUCHSIA_TCTI_INCLUDE_FUCHSIA_TCTI_H_ */
