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

bool MagmaSystemContext::ExecuteCommandBuffer(std::shared_ptr<MagmaSystemBuffer> command_buffer)
{

    // copy command buffer before validating to avoid tampering after validating
    // TODO(MA-111) use Copy On Write here if possible
    auto command_buffer_copy =
        MagmaSystemBuffer::Create(magma::PlatformBuffer::Create(command_buffer->size()));
    if (!command_buffer_copy)
        return DRETF(false, "ExecuteCommandBuffer: failed to create command buffer copy");

    void* cmd_buf_src;
    if (!command_buffer->platform_buffer()->MapCpu(&cmd_buf_src))
        return DRETF(false, "ExecuteCommandBuffer: Failed to map command buffer for copying");

    void* cmd_buf_dst;
    if (!command_buffer_copy->platform_buffer()->MapCpu(&cmd_buf_dst))
        return DRETF(false, "ExecuteCommandBuffer: Failed to map command buffer copy for copying");

    DASSERT(command_buffer->size() == command_buffer_copy->size());
    memcpy(cmd_buf_dst, cmd_buf_src, command_buffer->size());

    if (!command_buffer->platform_buffer()->UnmapCpu())
        return DRETF(false, "ExecuteCommandBuffer: Failed to unmap command buffer after copying");

    if (!command_buffer_copy->platform_buffer()->UnmapCpu())
        return DRETF(false,
                     "ExecuteCommandBuffer: Failed to unmap command buffer copy after copying");

    // we're done with our shared reference to the original buffer so we release it for good measure
    command_buffer.reset();

    auto cmd_buf = std::make_unique<MagmaSystemCommandBuffer>(std::move(command_buffer_copy));

    if (!cmd_buf->Initialize())
        return DRETF(false, "ExecuteCommandBuffer: Failed to initialize command buffer");

    // used to validate that handles are not duplicated
    std::unordered_set<uint32_t> id_set;

    // used to keep resources in scope until msd_context_execute_command_buffer returns
    std::vector<std::shared_ptr<MagmaSystemBuffer>> system_resources;
    system_resources.reserve(cmd_buf->num_resources());

    // the resources to be sent to the MSD driver
    auto msd_resources = std::vector<msd_buffer*>();
    msd_resources.reserve(cmd_buf->num_resources());

    // validate batch buffer index
    if (cmd_buf->batch_buffer_resource_index() >= cmd_buf->num_resources())
        return DRETF(false, "ExecuteCommandBuffer: batch buffer resource index invalid");

    // validate exec resources
    for (uint32_t i = 0; i < cmd_buf->num_resources(); i++) {
        uint64_t id = cmd_buf->resource(i).buffer_id();

        auto buf = owner_->LookupBufferForContext(id);
        if (!buf)
            return DRETF(false, "ExecuteCommandBuffer: exec resource has invalid buffer handle");

        auto iter = id_set.find(id);
        if (iter != id_set.end())
            return DRETF(false, "ExecuteCommandBuffer: duplicate exec resource");

        id_set.insert(id);
        system_resources.push_back(buf);
        msd_resources.push_back(buf->msd_buf());

        if (i == cmd_buf->batch_buffer_resource_index()) {
            // validate batch start
            if (cmd_buf->batch_start_offset() >= buf->size())
                return DRETF(false, "invalid batch start offset 0x%x",
                             cmd_buf->batch_start_offset());
        }
    }

    // validate relocations
    for (uint32_t res_index = 0; res_index < cmd_buf->num_resources(); res_index++) {
        auto resource = &cmd_buf->resource(res_index);

        for (uint32_t reloc_index = 0; reloc_index < resource->num_relocations(); reloc_index++) {
            auto relocation = resource->relocation(reloc_index);
            if (relocation->offset > system_resources[res_index]->size() - sizeof(uint32_t))
                return DRETF(false, "ExecuteCommandBuffer: relocation offset invalid");

            uint32_t target_index = relocation->target_resource_index;

            if (target_index >= cmd_buf->num_resources())
                return DRETF(false,
                             "ExecuteCommandBuffer: relocation target_resource_index invalid");

            if (relocation->target_offset >
                system_resources[target_index]->size() - sizeof(uint32_t))
                return DRETF(false, "ExecuteCommandBuffer: relocation target_offset invalid");
        }
    }

    // submit command buffer to driver
    int32_t ret = msd_context_execute_command_buffer(msd_ctx(), cmd_buf->system_buffer()->msd_buf(),
                                                     msd_resources.data());

    return DRETF(ret == 0, "ExecuteCommandBuffer: msd_context_execute_command_buffer failed");
}
