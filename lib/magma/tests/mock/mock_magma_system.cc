// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system.h"
#include "magma_util/dlog.h"

magma_system_connection* magma_system_open(int32_t fd) { return new magma_system_connection(); }

void magma_system_close(magma_system_connection* connection) { delete connection; }

int32_t magma_system_get_error(magma_system_connection* connection) { return 0; }

// Returns the device id.  0 is an invalid device id.
uint32_t magma_system_get_device_id(int32_t fd) { return 0; }

uint32_t next_context_id;
void magma_system_create_context(magma_system_connection* connection, uint32_t* context_id_out)
{
    *context_id_out = next_context_id++;
}

void magma_system_destroy_context(magma_system_connection* connection, uint32_t context_id) {}

uint32_t next_handle;
int32_t magma_system_alloc(magma_system_connection* connection, uint64_t size, uint64_t* size_out,
                           uint32_t* handle_out)
{
    *size_out = size;
    *handle_out = next_handle++;
    return 0;
}

void magma_system_free(magma_system_connection* connection, uint32_t handle) {}

void magma_system_set_tiling_mode(magma_system_connection* connection, uint32_t handle,
                                  uint32_t tiling_mode)
{
}

int32_t magma_system_map(magma_system_connection* connection, uint32_t handle, void** paddr)
{
    return 0;
}

int32_t magma_system_unmap(magma_system_connection* connection, uint32_t handle, void* addr)
{
    return 0;
}

void magma_system_set_domain(magma_system_connection* connection, uint32_t handle,
                             uint32_t read_domains, uint32_t write_domain)
{
}

void magma_system_submit_command_buffer(struct magma_system_connection* connection,
                                        struct magma_system_command_buffer* command_buffer,
                                        uint32_t context_id)
{
}

void magma_system_wait_rendering(magma_system_connection* connection, uint32_t handle)
{
}

void magma_system_export(magma_system_connection* connection, uint32_t handle, uint32_t* token_out)
{
}
