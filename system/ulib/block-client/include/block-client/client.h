// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <zircon/device/block.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct fifo_client fifo_client_t;

// Allocates a block fifo client. The client is thread-safe, as long
// as each thread accessing the client uses a distinct reqid.
//
// Valid groups are in the range [0, MAX_TXN_GROUP_COUNT).
zx_status_t block_fifo_create_client(zx_handle_t fifo, fifo_client_t** out);

// Frees a block fifo client
void block_fifo_release_client(fifo_client_t* client);

// Sends 'count' block device requests and waits for a response.
// The current implementation is thread-safe, but may only be called from a
// single process, as it differentiates callers by stack addresses (in an
// effort to make each transaction require no heap allocation).
//
// Each of the requests should set the following:
// FIELD                                    OPS
// --------------------------------------   -----------------
// group                                    All  (must be the same for all requests)
// vmoid                                    All
// opcode (BLOCKIO_OP_MASK bits only)       All
// length                                   read, write
// vmo_offset                               read, write
// dev_offset                               read, write
zx_status_t block_fifo_txn(fifo_client_t* client, block_fifo_request_t* requests, size_t count);

__END_CDECLS
