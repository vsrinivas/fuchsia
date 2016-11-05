// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_COMMAND_BUFFER_H
#define UTIL_COMMAND_BUFFER_H

#include "magma_system_common_defs.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"
#include <vector>

namespace magma {

class CommandBuffer {
public:
    virtual PlatformBuffer* platform_buffer() = 0;

    bool Initialize();

    uint32_t batch_buffer_resource_index() const
    {
        DASSERT(vaddr_);
        return reinterpret_cast<magma_system_command_buffer*>(vaddr_)->batch_buffer_resource_index;
    }

    uint32_t num_resources() const
    {
        DASSERT(vaddr_);
        return reinterpret_cast<magma_system_command_buffer*>(vaddr_)->num_resources;
    }

    class ExecResource {
    public:
        ExecResource(magma_system_exec_resource* resource,
                     magma_system_relocation_entry* relocations)
            : resource_(resource), relocations_(relocations)
        {
        }

        uint32_t buffer_handle() const { return resource_->buffer_handle; }

        uint32_t num_relocations() const { return resource_->num_relocations; }

        uint64_t offset() const { return resource_->offset; }

        uint64_t length() const { return resource_->length; }

        magma_system_relocation_entry* relocation(uint32_t relocation_index) const
        {
            DASSERT(relocation_index < num_relocations());
            return relocations_ + relocation_index;
        }

    private:
        magma_system_exec_resource* resource_;
        magma_system_relocation_entry* relocations_;
    };

    const ExecResource& resource(uint32_t resource_index) const
    {
        DASSERT(initialized_);
        DASSERT(resource_index < num_resources())
        return resources_[resource_index];
    }

private:
    void* vaddr_ = nullptr;
    bool initialized_ = false;
    std::vector<ExecResource> resources_;
};
}

#endif