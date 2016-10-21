// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMMAND_BUFFER_H
#define COMMAND_BUFFER_H

#include "mapped_batch.h"
#include "msd.h"
#include "msd_intel_buffer.h"

#include <memory>
#include <vector>

class AddressSpace;
class ClientContext;
class EngineCommandStreamer;
class MsdIntelContext;

class CommandBuffer : public MappedBatch {
public:
    // Takes a weak reference on the context which it locks for the duration of its execution
    static std::unique_ptr<CommandBuffer> Create(magma_system_command_buffer* cmd_buf,
                                                 msd_buffer** exec_resources,
                                                 std::weak_ptr<ClientContext> context)
    {
        return std::unique_ptr<CommandBuffer>(new CommandBuffer(cmd_buf, exec_resources, context));
    }

    ~CommandBuffer();

    // Map all execution resources into the given address space, patches relocations based on the
    // mapped addresses, and locks the weak reference to the context for the rest of the lifetime
    // of this object
    // this should be called only when we are ready to submit the CommandBuffer for execution
    bool PrepareForExecution(EngineCommandStreamer* engine);

    // only valid after PrepareForExecution succeeds
    MsdIntelContext* GetContext() override;

    void SetSequenceNumber(uint32_t sequence_number) override;

    bool GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* gpu_addr_out) override;

private:
    CommandBuffer(magma_system_command_buffer* cmd_buf, msd_buffer** exec_resources,
                  std::weak_ptr<ClientContext> context);

    // maps all execution resources into the given |address_space|.
    // fills |resource_gpu_addresses_out| with the mapped addresses of every object in
    // exec_resources_
    bool MapResourcesGpu(std::shared_ptr<AddressSpace> address_space,
                         std::vector<std::shared_ptr<GpuMapping>>& mappings);

    void UnmapResourcesGpu();

    // given the virtual addresses of all of the exec_resources_, walks the relocations data
    // structure in
    // cmd_buf_ and patches the correct virtual addresses into the corresponding buffers
    bool PatchRelocations(std::vector<std::shared_ptr<GpuMapping>>& mappings);

    // utility function used by PatchRelocations to perform the actual relocation for a single entry
    static bool PatchRelocation(magma_system_relocation_entry* relocation, MsdIntelBuffer* resource,
                                gpu_addr_t target_gpu_address);

    // TODO (MA-70) cmd_buf should be uniquely owned here
    magma_system_command_buffer* cmd_buf_;
    std::vector<std::shared_ptr<MsdIntelBuffer>> exec_resources_;
    std::vector<std::shared_ptr<GpuMapping>> exec_resource_mappings_;
    std::weak_ptr<ClientContext> context_;

    bool prepared_to_execute_;
    // valid only when prepared_to_execute_ is true
    std::shared_ptr<ClientContext> locked_context_;
    gpu_addr_t batch_buffer_gpu_addr_;
    EngineCommandStreamerId engine_id_;
    uint32_t sequence_number_ = Sequencer::kInvalidSequenceNumber;
    // ---------------------------- //

    friend class TestCommandBuffer;
};

#endif // COMMAND_BUFFER_H
