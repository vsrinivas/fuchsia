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

/// This library has to be depended on by the TSS 2.0 interface written in
/// plain C. So we must provide a C compatible interface while internally
/// holding C++ types.
/// See: //third_party/tpm2-tss/src/src/tss2-tcti/tcti-fuchsia.c for the
/// integration point.
typedef void opaque_ctx_t;

/// Initializes the Fuchsia TCTI interface returning an opaque_ctx.
/// This is intended to be the general purpose interface that connects to a
/// TPM transport. The ownership of opaque_ctx_t is transferred to the caller.
opaque_ctx_t* fuchsia_tpm_init(void);

/// Calls the TPM FIDL protocol to sending `buffer_len` bytes of `buffer`
/// across the channel.
///
/// Return Value:
/// - 0 indicates a successful command invocation.
/// - Any other response represents a TSS2_RC error code.
int fuchsia_tpm_send(opaque_ctx_t* context, int command_code, const uint8_t* buffer,
                     size_t buffer_len);

/// Attempts to extract `out_buffer_len` bytes into the `out_buffer`.
/// This function does not perform a FIDL request but instead reads from its
/// own recv_buffer stored in the context that is appended to from the results
/// of fuchsia_tpm_send.
///
/// Return Value:
/// The number of bytes written to `out_buffer`.
size_t fuchsia_tpm_recv(opaque_ctx_t* context, uint8_t* out_buffer, size_t out_buffer_len);

/// Frees the underlying memory structures from the context and closes any
/// open handles.
void fuchsia_tpm_finalize(opaque_ctx_t* context);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SRC_SECURITY_LIB_FUCHSIA_TCTI_INCLUDE_FUCHSIA_TCTI_H_ */
