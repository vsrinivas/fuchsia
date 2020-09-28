// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEE_CLIENT_API_TEE_CLIENT_IMPL_H_
#define TEE_CLIENT_API_TEE_CLIENT_IMPL_H_

#include <zircon/types.h>

/*
 * TEE Client Implementation
 *
 * This file provides the Implementation specific structures necessary to complete the TEE
 * Client API.
 */

/* Maximum number of parameters that can be specified in an TEEC_Operation. */
#define TEEC_NUM_PARAMS_MAX 4

typedef struct teec_context_impl {
  // TODO(fxbug.dev/36236): Currently, some driver code is directly setting tee_channel
  // instead of using TEEC_InitializeContext() (since that doesn't work in
  // driver code).
  zx_handle_t tee_channel;
} teec_context_impl_t;

typedef struct teec_session_impl {
  uint32_t session_id;
  teec_context_impl_t* context_imp;
} teec_session_impl_t;

typedef struct teec_shared_memory_impl {
  zx_handle_t vmo;
  zx_vaddr_t mapped_addr;
  size_t mapped_size;
} teec_shared_memory_impl_t;

typedef struct teec_operation_impl {
  /* This is just a placeholder so that the struct is not empty. */
  char reserved;
} teec_operation_impl_t;

#endif  // TEE_CLIENT_API_TEE_CLIENT_IMPL_H_
