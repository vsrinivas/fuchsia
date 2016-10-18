// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_context.h"
#include "magma_system_connection.h"
#include "magma_system_device.h"
#include "magma_util/macros.h"

#include <memory>
#include <unordered_set>
#include <vector>

bool MagmaSystemContext::ExecuteCommandBuffer(magma_system_command_buffer* cmd_buf)
{

    // used to validate that handles are not duplicated
    std::unordered_set<uint32_t> handle_set;

    // used to keep resources in scope until msd_context_execute_command_buffer returns
    std::vector<std::shared_ptr<MagmaSystemBuffer>> system_resources;
    system_resources.reserve(cmd_buf->num_resources);

    // the resources to be sent to the MSD driver
    auto msd_resources = std::vector<msd_buffer*>();
    msd_resources.reserve(cmd_buf->num_resources);

    // validate batch buffer index
    if (cmd_buf->batch_buffer_resource_index >= cmd_buf->num_resources)
        return DRETF(false, "ExecuteCommandBuffer: batch buffer resource index invalid");

    // validate exec resources
    for (uint32_t i = 0; i < cmd_buf->num_resources; i++) {
        uint32_t handle = cmd_buf->resources[i].buffer_handle;

        uint64_t id;
        if (!magma::PlatformBuffer::IdFromHandle(handle, &id))
            return DRETF(false, "ExecuteCommandBuffer: batch buffer handle invalid");

        auto buf = owner_->LookupBufferForContext(id);
        if (!buf)
            return DRETF(false, "ExecuteCommandBuffer: exec resource has invalid buffer handle");

        auto iter = handle_set.find(handle);
        if (iter != handle_set.end())
            return DRETF(false, "ExecuteCommandBuffer: duplicate exec resource");

        handle_set.insert(handle);
        system_resources.push_back(buf);
        msd_resources.push_back(buf->msd_buf());
    }

    // validate relocations
    for (uint32_t res_index = 0; res_index < cmd_buf->num_resources; res_index++) {
        auto resource = &cmd_buf->resources[res_index];

        for (uint32_t reloc_index = 0; reloc_index < resource->num_relocations; reloc_index++) {
            auto relocation = &resource->relocations[reloc_index];
            if (relocation->offset > system_resources[res_index]->size() - sizeof(uint32_t))
                return DRETF(false, "ExecuteCommandBuffer: relocation offset invalid");

            uint32_t target_index = relocation->target_resource_index;

            if (target_index > resource->num_relocations)
                return DRETF(false,
                             "ExecuteCommandBuffer: relocation target_resource_index invalid");

            if (relocation->target_offset >
                system_resources[target_index]->size() - sizeof(uint32_t))
                return DRETF(false, "ExecuteCommandBuffer: relocation target_offset invalid");
        }
    }

    // submit command buffer to driver
    int32_t ret = msd_context_execute_command_buffer(msd_ctx(), cmd_buf, msd_resources.data());

    return DRETF(ret == 0, "ExecuteCommandBuffer: msd_context_execute_command_buffer failed");
}
