// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

/*
 * TEE Client Implementation
 *
 * This file provides the Implementation specific structures necessary to complete the TEE
 * Client API.
 */

typedef struct teec_context_impl {
    int fd;
} teec_context_impl_t;

typedef struct teec_session_impl {
} teec_session_impl_t;

typedef struct teec_shared_memory_impl {
} teec_shared_memory_impl_t;

typedef struct teec_operation_impl {
} teec_operation_impl_t;
