// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_COMMAND_BUFFER_H
#define TEST_COMMAND_BUFFER_H

#include "command_buffer.h"
#include "msd_intel_device.h"

class TestCommandBuffer {
 public:
  static std::unique_ptr<CommandBuffer> Create(
      std::shared_ptr<MsdIntelBuffer> command_buffer_descriptor,
      std::weak_ptr<ClientContext> context, std::vector<std::shared_ptr<MsdIntelBuffer>> buffers,
      std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
      std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores) {
    void* ptr;
    if (!command_buffer_descriptor->platform_buffer()->MapCpu(&ptr))
      return DRETP(nullptr, "MapCpu failed");

    auto cmd_buf_ptr = reinterpret_cast<magma_system_command_buffer*>(ptr);
    auto semaphores_ptr = reinterpret_cast<uint64_t*>(cmd_buf_ptr + 1);
    auto exec_resources_ptr = reinterpret_cast<magma_system_exec_resource*>(
        semaphores_ptr + cmd_buf_ptr->wait_semaphore_count + cmd_buf_ptr->signal_semaphore_count);

    auto command_buffer = std::unique_ptr<CommandBuffer>(
        new CommandBuffer(context, std::make_unique<magma_system_command_buffer>(*cmd_buf_ptr)));

    std::vector<CommandBuffer::ExecResource> resources;
    resources.reserve(cmd_buf_ptr->resource_count);
    for (uint32_t i = 0; i < cmd_buf_ptr->resource_count; i++) {
      resources.emplace_back(CommandBuffer::ExecResource{buffers[i], exec_resources_ptr[i].offset,
                                                         exec_resources_ptr[i].length});
    }

    if (!command_buffer->InitializeResources(std::move(resources), std::move(wait_semaphores),
                                             std::move(signal_semaphores)))
      return DRETP(nullptr, "failed to initialize command buffer resources");

    return command_buffer;
  }

  static bool MapResourcesGpu(CommandBuffer* command_buffer,
                              std::shared_ptr<AddressSpace> address_space,
                              std::vector<std::shared_ptr<GpuMapping>>& mappings) {
    return command_buffer->MapResourcesGpu(address_space, mappings);
  }

  static void UnmapResourcesGpu(CommandBuffer* command_buffer) {
    return command_buffer->UnmapResourcesGpu();
  }

  static uint32_t batch_buffer_resource_index(const CommandBuffer* command_buffer) {
    return command_buffer->batch_buffer_resource_index();
  }

  static std::vector<CommandBuffer::ExecResource>& exec_resources(CommandBuffer* command_buffer) {
    return command_buffer->exec_resources_;
  }

  // TODO(MA-208) - move this
  static RenderEngineCommandStreamer* render_engine(MsdIntelDevice* device) {
    return device->render_engine_cs();
  }

  // TODO(MA-208) - move this
  static void StartDeviceThread(MsdIntelDevice* device) { device->StartDeviceThread(); }

  static bool InitContextForRender(MsdIntelDevice* device, MsdIntelContext* context) {
    return device->InitContextForRender(context);
  }
};

#endif  // TEST_COMMAND_BUFFER_H
