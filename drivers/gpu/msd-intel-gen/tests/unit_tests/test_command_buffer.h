// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_COMMAND_BUFFER_H
#define TEST_COMMAND_BUFFER_H

#include "command_buffer.h"
#include "msd_intel_device.h"

class TestCommandBuffer {
public:
    static std::unique_ptr<CommandBuffer>
    Create(std::shared_ptr<MsdIntelBuffer> command_buffer_descriptor,
           std::weak_ptr<ClientContext> context,
           std::vector<std::shared_ptr<MsdIntelBuffer>> buffers,
           std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
           std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores)
    {

        auto command_buffer = std::unique_ptr<CommandBuffer>(
            new CommandBuffer(std::move(command_buffer_descriptor), context));

        if (!command_buffer->Initialize())
            return DRETP(nullptr, "failed to initialize command buffer");

        if (!command_buffer->InitializeResources(std::move(buffers), std::move(wait_semaphores),
                                                 std::move(signal_semaphores)))
            return DRETP(nullptr, "failed to initialize command buffer resources");

        return command_buffer;
    }

    static magma::PlatformBuffer* platform_buffer(CommandBuffer* command_buffer)
    {
        return command_buffer->platform_buffer();
    }

    static bool MapResourcesGpu(CommandBuffer* command_buffer,
                                std::shared_ptr<AddressSpace> address_space,
                                std::vector<std::shared_ptr<GpuMapping>>& mappings)
    {
        return command_buffer->MapResourcesGpu(address_space, mappings);
    }

    static void UnmapResourcesGpu(CommandBuffer* command_buffer)
    {
        return command_buffer->UnmapResourcesGpu();
    }

    static uint32_t batch_buffer_resource_index(const CommandBuffer* command_buffer)
    {
        return command_buffer->batch_buffer_resource_index();
    }

    static std::vector<CommandBuffer::ExecResource>& exec_resources(CommandBuffer* command_buffer)
    {
        return command_buffer->exec_resources_;
    }

    static const magma::CommandBuffer::ExecResource& resource(const CommandBuffer* command_buffer,
                                                              uint32_t resource_index)
    {
        return command_buffer->resource(resource_index);
    }

    static bool PatchRelocations(CommandBuffer* command_buffer,
                                 std::vector<std::shared_ptr<GpuMapping>>& mappings)
    {
        return command_buffer->PatchRelocations(mappings);
    }

    // TODO(MA-208) - move this
    static RenderEngineCommandStreamer* render_engine(MsdIntelDevice* device)
    {
        return device->render_engine_cs();
    }

    // TODO(MA-208) - move this
    static void StartDeviceThread(MsdIntelDevice* device) { device->StartDeviceThread(); }
};

#endif // TEST_COMMAND_BUFFER_H
