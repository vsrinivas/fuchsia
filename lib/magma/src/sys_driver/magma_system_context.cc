// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_context.h"
#include "magma_system_connection.h"
#include "magma_system_device.h"
#include "magma_util/command_buffer.h"
#include "magma_util/macros.h"

#include <memory>
#include <unordered_set>
#include <vector>

class MagmaSystemCommandBuffer : public magma::CommandBuffer {
public:
    MagmaSystemCommandBuffer(std::unique_ptr<MagmaSystemBuffer> buffer) : buffer_(std::move(buffer))
    {
    }

    magma::PlatformBuffer* platform_buffer() override { return buffer_->platform_buffer(); }

    MagmaSystemBuffer* system_buffer() { return buffer_.get(); }

private:
    std::unique_ptr<MagmaSystemBuffer> buffer_;
};

magma::Status
MagmaSystemContext::ExecuteCommandBuffer(std::shared_ptr<MagmaSystemBuffer> command_buffer)
{

    // copy command buffer before validating to avoid tampering after validating
    // TODO(MA-111) use Copy On Write here if possible
    auto command_buffer_copy =
        MagmaSystemBuffer::Create(magma::PlatformBuffer::Create(command_buffer->size()));
    if (!command_buffer_copy)
        return DRET_MSG(MAGMA_STATUS_MEMORY_ERROR,
                        "ExecuteCommandBuffer: failed to create command buffer copy");

    void* cmd_buf_src;
    if (!command_buffer->platform_buffer()->MapCpu(&cmd_buf_src))
        return DRET_MSG(MAGMA_STATUS_MEMORY_ERROR,
                        "ExecuteCommandBuffer: Failed to map command buffer for copying");

    void* cmd_buf_dst;
    if (!command_buffer_copy->platform_buffer()->MapCpu(&cmd_buf_dst))
        return DRET_MSG(MAGMA_STATUS_MEMORY_ERROR,
                        "ExecuteCommandBuffer: Failed to map command buffer copy for copying");

    DASSERT(command_buffer->size() == command_buffer_copy->size());
    memcpy(cmd_buf_dst, cmd_buf_src, command_buffer->size());

    if (!command_buffer->platform_buffer()->UnmapCpu())
        return DRET_MSG(MAGMA_STATUS_MEMORY_ERROR,
                        "ExecuteCommandBuffer: Failed to unmap command buffer after copying");

    if (!command_buffer_copy->platform_buffer()->UnmapCpu())
        return DRET_MSG(MAGMA_STATUS_MEMORY_ERROR,
                        "ExecuteCommandBuffer: Failed to unmap command buffer copy after copying");

    // we're done with our shared reference to the original buffer so we release it for good measure
    command_buffer.reset();

    auto cmd_buf = std::make_unique<MagmaSystemCommandBuffer>(std::move(command_buffer_copy));

    if (!cmd_buf->Initialize())
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                        "ExecuteCommandBuffer: Failed to initialize command buffer");

    // used to validate that handles are not duplicated
    std::unordered_set<uint32_t> id_set;

    // used to keep resources in scope until msd_context_execute_command_buffer returns
    std::vector<std::shared_ptr<MagmaSystemBuffer>> system_resources;
    system_resources.reserve(cmd_buf->num_resources());

    // the resources to be sent to the MSD driver
    auto msd_resources = std::vector<msd_buffer_t*>();
    msd_resources.reserve(cmd_buf->num_resources());

    // validate batch buffer index
    if (cmd_buf->batch_buffer_resource_index() >= cmd_buf->num_resources())
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                        "ExecuteCommandBuffer: batch buffer resource index invalid");

    // validate exec resources
    for (uint32_t i = 0; i < cmd_buf->num_resources(); i++) {
        uint64_t id = cmd_buf->resource(i).buffer_id();

        auto buf = owner_->LookupBufferForContext(id);
        if (!buf)
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                            "ExecuteCommandBuffer: exec resource has invalid buffer handle");

        auto iter = id_set.find(id);
        if (iter != id_set.end())
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                            "ExecuteCommandBuffer: duplicate exec resource");

        id_set.insert(id);
        system_resources.push_back(buf);
        msd_resources.push_back(buf->msd_buf());

        if (i == cmd_buf->batch_buffer_resource_index()) {
            // validate batch start
            if (cmd_buf->batch_start_offset() >= buf->size())
                return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "invalid batch start offset 0x%x",
                                cmd_buf->batch_start_offset());
        }
    }

    // validate relocations
    for (uint32_t res_index = 0; res_index < cmd_buf->num_resources(); res_index++) {
        auto resource = &cmd_buf->resource(res_index);

        for (uint32_t reloc_index = 0; reloc_index < resource->num_relocations(); reloc_index++) {
            auto relocation = resource->relocation(reloc_index);
            if (relocation->offset > system_resources[res_index]->size() - sizeof(uint32_t))
                return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                                "ExecuteCommandBuffer: relocation offset invalid");

            uint32_t target_index = relocation->target_resource_index;

            if (target_index >= cmd_buf->num_resources())
                return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                                "ExecuteCommandBuffer: relocation target_resource_index invalid");

            if (relocation->target_offset >
                system_resources[target_index]->size() - sizeof(uint32_t))
                return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                                "ExecuteCommandBuffer: relocation target_offset invalid");
        }
    }

    // used to keep semaphores in scope until msd_context_execute_command_buffer returns
    std::vector<msd_semaphore_t*> msd_wait_semaphores(cmd_buf->wait_semaphore_count());
    std::vector<msd_semaphore_t*> msd_signal_semaphores(cmd_buf->signal_semaphore_count());

    // validate semaphores
    for (uint32_t i = 0; i < cmd_buf->wait_semaphore_count(); i++) {
        auto semaphore = owner_->LookupSemaphoreForContext(cmd_buf->wait_semaphore_id(i));
        if (!semaphore)
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "wait semaphore id not found 0x%" PRIx64,
                            cmd_buf->wait_semaphore_id(i));
        msd_wait_semaphores[i] = semaphore->msd_semaphore();
    }
    for (uint32_t i = 0; i < cmd_buf->signal_semaphore_count(); i++) {
        auto semaphore = owner_->LookupSemaphoreForContext(cmd_buf->signal_semaphore_id(i));
        if (!semaphore)
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "signal semaphore id not found 0x%" PRIx64,
                            cmd_buf->signal_semaphore_id(i));
        msd_signal_semaphores[i] = semaphore->msd_semaphore();
    }

    // submit command buffer to driver
    magma_status_t result = msd_context_execute_command_buffer(
        msd_ctx(), cmd_buf->system_buffer()->msd_buf(), msd_resources.data(),
        msd_wait_semaphores.data(), msd_signal_semaphores.data());

    return DRET_MSG(result, "ExecuteCommandBuffer: msd_context_execute_command_buffer failed: %d",
                    result);
}
