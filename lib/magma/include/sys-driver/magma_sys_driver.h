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

#ifndef _MAGMA_SYS_DRIVER_H_
#define _MAGMA_SYS_DRIVER_H_

#include <magma_sys_defs.h>
#include <platform_buffer_abi.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct MagmaSysDriver;
struct MsdDevice;

// Instantiates a driver instance.
struct MagmaSysDriver* msd_create(void);
void msd_destroy(struct MagmaSysDriver* arch);

// Creates a device - triggered by device discovery.
struct MsdDevice* msd_create_device(struct MagmaSysDriver* arch, void* device);
void msd_destroy_device(struct MsdDevice*);

// Opens a device - triggered by a client action.
bool msd_open(struct MsdDevice* dev, ClientId client_id);
void msd_close(struct MsdDevice* dev, ClientId client_id);

// Returns the device id.  0 is an invalid device id.
uint32_t msd_get_device_id(struct MsdDevice* dev);

bool msd_create_context(struct MsdDevice* dev, ClientId client_id, int* context_id);

bool msd_set_tiling_mode(struct MsdDevice* dev, uint32_t handle, uint32_t tiling_mode);

bool msd_set_domain(struct MsdDevice* dev, uint32_t handle, uint32_t read_domains,
                    uint32_t write_domain);

bool msd_subdata(struct MsdDevice* dev, uint32_t handle, unsigned long offset, unsigned long size,
                 const void* data);

bool msd_execute_buffer(struct MsdDevice* dev, ClientId client_id,
                        struct MagmaExecBuffer* execbuffer);

void msd_wait_rendering(struct MsdDevice* dev, uint32_t handle);

#if defined(__cplusplus)
}
#endif

#endif /* _MAGMA_SYS_DRIVER_H_ */
