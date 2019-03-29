// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMMAND_BUFFER_H
#define COMMAND_BUFFER_H

#include "magma_util/command_buffer.h"
#include "mapped_batch.h"
#include "msd.h"
#include "msd_intel_buffer.h"
#include "platform_semaphore.h"

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
    static std::unique_ptr<CommandBuffer> Create(msd_buffer_t* abi_cmd_buf,
                                                 msd_buffer_t** msd_buffers,
                                                 std::weak_ptr<ClientContext> context,
                                                 msd_semaphore_t** msd_wait_semaphores,
                                                 msd_semaphore_t** msd_signal_semaphores);

    ~CommandBuffer();

    // Map all execution resources into the gpu address space, patches relocations based on the
    // mapped addresses, and locks the weak reference to the context for the rest of the lifetime
    // of this object.
    bool PrepareForExecution();

    std::weak_ptr<MsdIntelContext> GetContext() override;

    void SetSequenceNumber(uint32_t sequence_number) override;

    bool GetGpuAddress(gpu_addr_t* gpu_addr_out) override;

    uint64_t GetBatchBufferId() override;

    uint32_t GetPipeControlFlags() override;

    // Takes ownership of the wait semaphores array
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores()
    {
        return std::move(wait_semaphores_);
    }

    void GetMappings(std::vector<GpuMappingView*>* mappings_out)
    {
        mappings_out->clear();
        for (const auto& mapping : exec_resource_mappings_) {
            mappings_out->emplace_back(mapping.get());
        }
    }

    const GpuMappingView* GetBatchMapping() override
    {
        DASSERT(prepared_to_execute_);
        return exec_resource_mappings_[batch_buffer_index_].get();
    }

private:
    CommandBuffer(std::shared_ptr<MsdIntelBuffer> abi_cmd_buf,
                  std::weak_ptr<ClientContext> context);

    bool IsCommandBuffer() override { return true; }

    // maps all execution resources into the given |address_space|.
    // fills |resource_gpu_addresses_out| with the mapped addresses of every object in
    // exec_resources_
    bool MapResourcesGpu(std::shared_ptr<AddressSpace> address_space,
                         std::vector<std::shared_ptr<GpuMapping>>& mappings);

    void UnmapResourcesGpu();

    bool
    InitializeResources(std::vector<std::shared_ptr<MsdIntelBuffer>> buffers,
                        std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
                        std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores);

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

    // magma::CommandBuffer implementation
    magma::PlatformBuffer* platform_buffer() override { return abi_cmd_buf_->platform_buffer(); }

    const std::shared_ptr<MsdIntelBuffer> abi_cmd_buf_;
    const std::weak_ptr<ClientContext> context_;
    const uint64_t nonce_;

    // Set on connection thread; valid only when prepared_to_execute_ is true
    bool prepared_to_execute_ = false;
    std::vector<ExecResource> exec_resources_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_;
    std::vector<std::shared_ptr<GpuMapping>> exec_resource_mappings_;
    std::shared_ptr<ClientContext> locked_context_;
    uint32_t batch_buffer_index_;
    uint32_t batch_start_offset_;
    // ---------------------------- //

    // Set on device thread
    uint32_t sequence_number_ = Sequencer::kInvalidSequenceNumber;

    friend class TestCommandBuffer;
};

#endif // COMMAND_BUFFER_H
