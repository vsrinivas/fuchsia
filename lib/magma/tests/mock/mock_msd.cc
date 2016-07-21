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

#include "magma_sys_driver.h"

void msd_destroy(MagmaSysDriver* arch) {}

MsdDevice* msd_create_device(MagmaSysDriver* arch, void* device) { return nullptr; }

void msd_destroy_device(MsdDevice*) { return; }

bool msd_open(MsdDevice* dev, ClientId client_id) { return false; }

void msd_close(MsdDevice* dev, ClientId client_id) { return; }

uint32_t msd_get_device_id(MsdDevice* dev) { return 0; }

bool msd_create_context(MsdDevice* dev, ClientId client_id, int* context_id) { return false; }

bool msd_set_tiling_mode(MsdDevice* dev, uint32_t handle, uint32_t tiling_mode) { return false; }

bool msd_set_domain(MsdDevice* dev, uint32_t handle, uint32_t read_domains, uint32_t write_domain)
{
    return false;
}

bool msd_subdata(MsdDevice* dev, uint32_t handle, unsigned long offset, unsigned long size,
                 const void* data)
{
    return false;
}

bool msd_execute_buffer(MsdDevice* dev, ClientId client_id, MagmaExecBuffer* execbuffer)
{
    return false;
}

void msd_wait_rendering(MsdDevice* dev, uint32_t handle) { return; }