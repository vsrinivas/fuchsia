// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_buffer.h"

#include "address_space.h"
#include "instructions.h"
#include "msd_intel_connection.h"
#include "msd_intel_context.h"
#include "msd_intel_semaphore.h"
#include "platform_trace.h"

std::unique_ptr<CommandBuffer> CommandBuffer::Create(std::weak_ptr<ClientContext> context,
                                                     magma_system_command_buffer* cmd_buf,
                                                     magma_system_exec_resource* exec_resources,
                                                     msd_buffer_t** msd_buffers,
                                                     msd_semaphore_t** msd_wait_semaphores,
                                                     msd_semaphore_t** msd_signal_semaphores) {
  if (cmd_buf->resource_count == 0)
    return DRETP(nullptr, "Command buffer requires at least 1 resource");

  std::vector<ExecResource> resources;
  resources.reserve(cmd_buf->resource_count);
  for (uint32_t i = 0; i < cmd_buf->resource_count; i++) {
    resources.emplace_back(ExecResource{MsdIntelAbiBuffer::cast(msd_buffers[i])->ptr(),
                                        exec_resources[i].offset, exec_resources[i].length});
  }

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
  wait_semaphores.reserve(cmd_buf->wait_semaphore_count);
  for (uint32_t i = 0; i < cmd_buf->wait_semaphore_count; i++) {
    wait_semaphores.emplace_back(MsdIntelAbiSemaphore::cast(msd_wait_semaphores[i])->ptr());
  }

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  signal_semaphores.reserve(cmd_buf->signal_semaphore_count);
  for (uint32_t i = 0; i < cmd_buf->signal_semaphore_count; i++) {
    signal_semaphores.emplace_back(MsdIntelAbiSemaphore::cast(msd_signal_semaphores[i])->ptr());
  }

  auto command_buffer = std::unique_ptr<CommandBuffer>(
      new CommandBuffer(context, std::make_unique<magma_system_command_buffer>(*cmd_buf)));

  if (!command_buffer->InitializeResources(std::move(resources), std::move(wait_semaphores),
                                           std::move(signal_semaphores)))
    return DRETP(nullptr, "failed to initialize command buffer resources");

  return command_buffer;
}

CommandBuffer::CommandBuffer(std::weak_ptr<ClientContext> context,
                             std::unique_ptr<magma_system_command_buffer> cmd_buf)
    : context_(context), command_buffer_(std::move(cmd_buf)), nonce_(TRACE_NONCE()) {}

CommandBuffer::~CommandBuffer() {
  if (!prepared_to_execute_)
    return;

  std::shared_ptr<MsdIntelConnection> connection = locked_context_->connection().lock();
  uint64_t ATTRIBUTE_UNUSED connection_id = connection ? connection->client_id() : 0;
  uint64_t ATTRIBUTE_UNUSED current_ticks = magma::PlatformTrace::GetCurrentTicks();

  uint64_t ATTRIBUTE_UNUSED buffer_id = GetBatchBufferId();
  TRACE_DURATION("magma", "Command Buffer End");
  TRACE_VTHREAD_FLOW_STEP("magma", "command_buffer", "GPU", connection_id, buffer_id,
                          current_ticks);
  TRACE_FLOW_END("magma", "command_buffer", buffer_id);

  UnmapResourcesGpu();

  for (auto& semaphore : signal_semaphores_) {
    semaphore->Signal();
  }

  if (connection) {
    std::vector<uint64_t> buffer_ids(num_resources());
    for (uint32_t i = 0; i < num_resources(); i++) {
      buffer_ids[i] = exec_resources_[i].buffer->platform_buffer()->id();
    }
    connection->SendNotification(buffer_ids);
  }

  TRACE_ASYNC_END("magma-exec", "CommandBuffer Exec", nonce_);
}

void CommandBuffer::SetSequenceNumber(uint32_t sequence_number) {
  TRACE_ASYNC_BEGIN("magma-exec", "CommandBuffer Exec", nonce_, "id", GetBatchBufferId());
  sequence_number_ = sequence_number;
}

bool CommandBuffer::InitializeResources(
    std::vector<ExecResource> resources,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores) {
  TRACE_DURATION("magma", "InitializeResources");

  if (num_resources() != resources.size())
    return DRETF(false, "resources size mismatch");

  if (wait_semaphores.size() != wait_semaphore_count())
    return DRETF(false, "wait semaphore count mismatch");

  if (signal_semaphores.size() != signal_semaphore_count())
    return DRETF(false, "wait semaphore count mismatch");

  exec_resources_ = std::move(resources);
  wait_semaphores_ = std::move(wait_semaphores);
  signal_semaphores_ = std::move(signal_semaphores);

  return true;
}

std::weak_ptr<MsdIntelContext> CommandBuffer::GetContext() { return context_; }

uint32_t CommandBuffer::GetPipeControlFlags() {
  uint32_t flags = MiPipeControl::kCommandStreamerStallEnableBit;

  // Experimentally including this bit has been shown to resolve gpu faults where a batch
  // completes; we clear gtt mappings for resources; then on the next batch,
  // an invalid address is emitted corresponding to a cleared gpu mapping.  This was
  // first seen when a compute shader was introduced.
  flags |= MiPipeControl::kGenericMediaStateClearBit;

  // Similarly, including this bit was shown to resolve the emission of an invalid address.
  flags |= MiPipeControl::kIndirectStatePointersDisableBit;

  // This one is needed when l3 caching enabled via mocs (memory object control state).
  flags |= MiPipeControl::kDcFlushEnableBit;

  return flags;
}

bool CommandBuffer::GetGpuAddress(gpu_addr_t* gpu_addr_out) {
  if (!prepared_to_execute_)
    return DRETF(false, "not prepared to execute");

  *gpu_addr_out = exec_resource_mappings_[batch_buffer_index_]->gpu_addr() + batch_start_offset_;
  return true;
}

uint64_t CommandBuffer::GetBatchBufferId() {
  if (batch_buffer_resource_index() < exec_resources_.size())
    return exec_resources_[batch_buffer_resource_index()].buffer->platform_buffer()->id();
  return 0;
}

void CommandBuffer::UnmapResourcesGpu() { exec_resource_mappings_.clear(); }

bool CommandBuffer::PrepareForExecution() {
  locked_context_ = context_.lock();
  if (!locked_context_)
    return DRETF(false, "context has already been deleted, aborting");

  exec_resource_mappings_.clear();
  exec_resource_mappings_.reserve(exec_resources_.size());

  TRACE_FLOW_STEP("magma", "command_buffer", GetBatchBufferId());

  if (!MapResourcesGpu(locked_context_->exec_address_space(), exec_resource_mappings_))
    return DRETF(false, "failed to map execution resources");

  batch_buffer_index_ = batch_buffer_resource_index();
  batch_start_offset_ = batch_start_offset();

  prepared_to_execute_ = true;

  return true;
}

bool CommandBuffer::MapResourcesGpu(std::shared_ptr<AddressSpace> address_space,
                                    std::vector<std::shared_ptr<GpuMapping>>& mappings) {
  TRACE_DURATION("magma", "MapResourcesGpu");

  for (auto res : exec_resources_) {
    std::shared_ptr<GpuMapping> mapping =
        address_space->FindGpuMapping(res.buffer->platform_buffer(), res.offset, res.length);
    if (!mapping)
      return DRETF(false, "failed to find gpu mapping for buffer %lu",
                   res.buffer->platform_buffer()->id());
    DLOG("MapResourcesGpu aspace %p buffer 0x%" PRIx64 " offset 0x%" PRIx64 " length 0x%" PRIx64
         " gpu_addr 0x%" PRIx64,
         address_space.get(), res.buffer->platform_buffer()->id(), res.offset, res.length,
         mapping->gpu_addr());
    mappings.push_back(mapping);
  }

  return true;
}
