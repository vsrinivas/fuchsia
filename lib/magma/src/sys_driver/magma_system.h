// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _MAGMA_SYSTEM_H_
#define _MAGMA_SYSTEM_H_

#include <msd_defs.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct MagmaSystemConnection;

// Opens a device - triggered by a client action.
bool magma_system_open(struct MagmaSystemConnection** dev, uint32_t device_handle);
void magma_system_close(struct MagmaSystemConnection* dev);

// Returns the device id.  0 is an invalid device id.
uint32_t magma_system_get_device_id(struct MagmaSystemConnection* dev);

bool magma_system_create_context(struct MagmaSystemConnection* dev, uint32_t* context_id);
bool magma_system_destroy_context(struct MagmaSystemConnection* dev, uint32_t context_id);

bool magma_system_alloc(struct MagmaSystemConnection* dev, uint64_t size, uint64_t* size_out,
                        uint32_t* handle_out);
bool magma_system_free(struct MagmaSystemConnection* dev, uint32_t handle);

bool magma_system_set_tiling_mode(struct MagmaSystemConnection* dev, uint32_t handle,
                                  uint32_t tiling_mode);

bool magma_system_map(struct MagmaSystemConnection* dev, uint32_t handle, void** paddr);
bool magma_system_unmap(struct MagmaSystemConnection* dev, uint32_t handle, void* addr);

bool magma_system_set_domain(struct MagmaSystemConnection* dev, uint32_t handle, uint32_t read_domains,
                             uint32_t write_domain);

bool magma_system_execute_buffer(struct MagmaSystemConnection* dev, struct MagmaExecBuffer* execbuffer);

void magma_system_wait_rendering(struct MagmaSystemConnection* dev, uint32_t handle);

#if defined(__cplusplus)
}
#endif

#endif /* _MAGMA_SYSTEM_H_ */
