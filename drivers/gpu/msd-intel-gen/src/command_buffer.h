// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMMAND_BUFFER_H
#define COMMAND_BUFFER_H

#include "magma_util/command_buffer.h"
#include "mapped_batch.h"
#include "msd.h"
#include "msd_intel_buffer.h"

#include <memory>
#include <vector>

class AddressSpace;
class ClientContext;
class EngineCommandStreamer;
class MsdIntelContext;

class CommandBuffer : public MappedBatch, private magma::CommandBuffer {
public:
    // Takes a weak reference on the context which it locks for the duration of its execution
    // holds a shared reference to the buffers backing |abi_cmd_buf| and |exec_buffers| for the
    // lifetime of this object
    static std::unique_ptr<CommandBuffer> Create(msd_buffer* abi_cmd_buf, msd_buffer** exec_buffers,
                                                 std::weak_ptr<ClientContext> context)
    {
        auto command_buffer =
            std::unique_ptr<CommandBuffer>(new CommandBuffer(abi_cmd_buf, context));
        if (!command_buffer->Initialize(exec_buffers))
            return DRETP(nullptr, "failed to initialize command buffer");
        return command_buffer;
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
    CommandBuffer(msd_buffer* abi_cmd_buf, std::weak_ptr<ClientContext> context);

    // maps all execution resources into the given |address_space|.
    // fills |resource_gpu_addresses_out| with the mapped addresses of every object in
    // exec_resources_
    bool MapResourcesGpu(std::shared_ptr<AddressSpace> address_space,
                         std::vector<std::shared_ptr<GpuMapping>>& mappings);

    void UnmapResourcesGpu();

    bool Initialize(msd_buffer** exec_buffers);

    // given the virtual addresses of all of the exec_resources_, walks the relocations data
    // structure in
    // cmd_buf_ and patches the correct virtual addresses into the corresponding buffers
    bool PatchRelocations(std::vector<std::shared_ptr<GpuMapping>>& mappings);

    struct ExecResource {
        std::shared_ptr<MsdIntelBuffer> buffer;
        uint64_t offset;
        uint64_t length;
    };

    // utility function used by PatchRelocations to perform the actual relocation for a single entry
    static bool PatchRelocation(magma_system_relocation_entry* relocation,
                                ExecResource* exec_resource, gpu_addr_t target_gpu_address);

    std::shared_ptr<MsdIntelBuffer> abi_cmd_buf_;
    // magma::CommandBuffer implementation
    magma::PlatformBuffer* platform_buffer() override { return abi_cmd_buf_->platform_buffer(); }

    std::vector<ExecResource> exec_resources_;
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
