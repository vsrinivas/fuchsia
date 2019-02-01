// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_buffer.h"

namespace magma {

bool CommandBuffer::Initialize()
{
    if (initialized_)
        return true;

    if (!platform_buffer()->MapCpu(reinterpret_cast<void**>(&command_buffer_)))
        return DRETF(false, "Failed to map command buffer");
    DASSERT(command_buffer_);

    size_t max_size = platform_buffer()->size();
    size_t total_size = sizeof(magma_system_command_buffer);
    if (total_size > max_size)
        return DRETF(false, "Platform Buffer backing CommandBuffer is not large enough");

    total_size += sizeof(uint64_t) * (wait_semaphore_count() + signal_semaphore_count());
    if (total_size > max_size)
        return DRETF(false, "Platform Buffer backing CommandBuffer is not large enough");

    total_size += sizeof(magma_system_exec_resource) * num_resources();
    if (total_size > max_size)
        return DRETF(false, "Platform Buffer backing CommandBuffer is not large enough");

    resources_.reserve(num_resources());

    uint64_t* wait_semaphore_ids = reinterpret_cast<uint64_t*>(command_buffer_ + 1);

    uint64_t* signal_semaphore_ids = wait_semaphore_ids + wait_semaphore_count();

    magma_system_exec_resource* resource_base = reinterpret_cast<magma_system_exec_resource*>(
        signal_semaphore_ids + signal_semaphore_count());

    magma_system_relocation_entry* relocations_base =
        reinterpret_cast<magma_system_relocation_entry*>(resource_base + num_resources());

    for (uint32_t i = 0; i < num_resources(); i++) {
        auto num_relocations = resource_base->num_relocations;
        total_size += sizeof(magma_system_relocation_entry) * num_relocations;
        if (total_size > max_size)
            return DRETF(false, "Platform Buffer backing CommandBuffer is not large enough");

        resources_.emplace_back(ExecResource(resource_base, relocations_base));

        relocations_base += num_relocations;
        resource_base++;
    }

    initialized_ = true;
    return true;
}
} // namespace magma