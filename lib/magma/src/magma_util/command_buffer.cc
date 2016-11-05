// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_buffer.h"

namespace magma {

bool CommandBuffer::Initialize()
{
    if (initialized_)
        return true;

    if (!platform_buffer()->MapCpu(&vaddr_))
        return DRETF(false, "Failed to map command buffer");
    DASSERT(vaddr_);

    size_t max_size = platform_buffer()->size();
    size_t total_size = sizeof(magma_system_command_buffer);
    if (total_size > max_size)
        return DRETF(false, "Platform Buffer backing CommandBuffer is not large enough");

    total_size += sizeof(magma_system_exec_resource) * num_resources();
    if (total_size > max_size)
        return DRETF(false, "Platform Buffer backing CommandBuffer is not large enough");

    resources_.reserve(num_resources());

    magma_system_command_buffer* command_buffer_base =
        reinterpret_cast<magma_system_command_buffer*>(vaddr_);
    magma_system_exec_resource* resource_base =
        reinterpret_cast<magma_system_exec_resource*>(command_buffer_base + 1);
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
}