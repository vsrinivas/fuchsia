// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_TEE_TEE_CLIENT_API_INCLUDE_TEE_CLIENT_API_TEE_CLIENT_IMPL_H_
#define SRC_SECURITY_LIB_TEE_TEE_CLIENT_API_INCLUDE_TEE_CLIENT_API_TEE_CLIENT_IMPL_H_

#include <zircon/types.h>

/*
 * TEE Client Implementation
 *
 * This file provides the Implementation specific structures necessary to complete the TEE
 * Client API.
 *
 * Clients of the library should not rely on or modify the internal values of these structures.
 */

/* Maximum number of parameters that can be specified in an TEEC_Operation. */
#define TEEC_NUM_PARAMS_MAX 4

typedef struct teec_context_impl {
  // This channel is usually invalid, when client is connecting via a service. This channel will
  // be set when connecting directly to the driver.
  zx_handle_t device_connector_channel;

  // The UUID-keyed associative container that owns all of the open `fuchsia.tee.Application`
  // channels.
  void* uuid_to_channel;
} teec_context_impl_t;

typedef struct teec_session_impl {
  uint32_t session_id;

  // An unowned copy of the channel to `fuchsia.tee.Application` this session is attached to.
  zx_handle_t application_channel;
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

#endif  // SRC_SECURITY_TEE_TEE_CLIENT_API_INCLUDE_TEE_CLIENT_API_TEE_CLIENT_IMPL_H_
