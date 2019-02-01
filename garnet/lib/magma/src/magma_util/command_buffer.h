// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_COMMAND_BUFFER_H
#define UTIL_COMMAND_BUFFER_H

#include "magma_common_defs.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"
#include <vector>

namespace magma {

// Dictates the layout of a command buffer structure and associated data:
//  1) magma_system_command_buffer
//  2) array of wait semaphore ids
//  3) array of signal semaphore ids
//  4) array of exec resources
//  5) array of relocations (per resource)
//
class CommandBuffer {
public:
    virtual ~CommandBuffer() = default;

    virtual PlatformBuffer* platform_buffer() = 0;

    bool Initialize();

    bool initialized() { return initialized_; }

    uint32_t batch_buffer_resource_index() const
    {
        DASSERT(command_buffer_);
        return command_buffer_->batch_buffer_resource_index;
    }

    uint32_t num_resources() const
    {
        DASSERT(command_buffer_);
        return command_buffer_->num_resources;
    }

    uint32_t wait_semaphore_count() const { return command_buffer_->wait_semaphore_count; }

    uint32_t signal_semaphore_count() const { return command_buffer_->signal_semaphore_count; }

    uint32_t batch_start_offset() const
    {
        DASSERT(command_buffer_);
        return reinterpret_cast<magma_system_command_buffer*>(command_buffer_)->batch_start_offset;
    }

    class ExecResource {
    public:
        ExecResource(magma_system_exec_resource* resource,
                     magma_system_relocation_entry* relocations)
            : resource_(resource), relocations_(relocations)
        {
        }

        uint64_t buffer_id() const { return resource_->buffer_id; }

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
        DASSERT(resource_index < num_resources());
        return resources_[resource_index];
    }

    uint64_t wait_semaphore_id(uint32_t index) const
    {
        DASSERT(initialized_);
        DASSERT(index < wait_semaphore_count());
        uint64_t* wait_semaphores = reinterpret_cast<uint64_t*>(command_buffer_ + 1);
        return wait_semaphores[index];
    }

    uint64_t signal_semaphore_id(uint32_t index) const
    {
        DASSERT(initialized_);
        DASSERT(index < signal_semaphore_count());
        uint64_t* signal_semaphores =
            reinterpret_cast<uint64_t*>(command_buffer_ + 1) + wait_semaphore_count();
        return signal_semaphores[index];
    }

private:
    magma_system_command_buffer* command_buffer_ = nullptr;
    bool initialized_ = false;
    std::vector<ExecResource> resources_;
};
} // namespace magma

#endif
