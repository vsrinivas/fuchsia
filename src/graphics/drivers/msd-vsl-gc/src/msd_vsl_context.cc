// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsl_context.h"

#include "command_buffer.h"
#include "msd_vsl_semaphore.h"

// TODO(fxb/47800): ensure clients cannot map / unmap at the ringbuffer gpu address.
static constexpr uint32_t kRingbufferGpuAddr = 0x0;

// static
std::shared_ptr<MsdVslContext> MsdVslContext::Create(std::weak_ptr<MsdVslConnection> connection,
                                                     std::shared_ptr<AddressSpace> address_space,
                                                     Ringbuffer* ringbuffer) {
  auto context = std::make_shared<MsdVslContext>(connection, address_space);
  if (!context->MapRingbuffer(ringbuffer)) {
    return DRETP(nullptr, "failed to map ringbuffer into new context");
  }
  return context;
}

std::unique_ptr<MappedBatch> MsdVslContext::CreateBatch(std::shared_ptr<MsdVslContext> context,
                                                        magma_system_command_buffer* cmd_buf,
                                                        magma_system_exec_resource* exec_resources,
                                                        msd_buffer_t** msd_buffers,
                                                        msd_semaphore_t** msd_wait_semaphores,
                                                        msd_semaphore_t** msd_signal_semaphores) {
  std::vector<CommandBuffer::ExecResource> resources;
  resources.reserve(cmd_buf->num_resources);
  for (uint32_t i = 0; i < cmd_buf->num_resources; i++) {
    resources.emplace_back(CommandBuffer::ExecResource{MsdVslAbiBuffer::cast(msd_buffers[i])->ptr(),
                                                       exec_resources[i].offset,
                                                       exec_resources[i].length});
  }

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
  wait_semaphores.reserve(cmd_buf->wait_semaphore_count);
  for (uint32_t i = 0; i < cmd_buf->wait_semaphore_count; i++) {
    wait_semaphores.emplace_back(MsdVslAbiSemaphore::cast(msd_wait_semaphores[i])->ptr());
  }

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  signal_semaphores.reserve(cmd_buf->signal_semaphore_count);
  for (uint32_t i = 0; i < cmd_buf->signal_semaphore_count; i++) {
    signal_semaphores.emplace_back(MsdVslAbiSemaphore::cast(msd_signal_semaphores[i])->ptr());
  }

  auto connection = context->connection().lock();
  if (!connection) {
    return DRETP(nullptr, "Connection is already dead");
  }

  std::unique_ptr<MappedBatch> batch;

  // The CommandBuffer does not support batches with zero resources.
  if (resources.size() > 0) {
    auto command_buffer = std::make_unique<CommandBuffer>(
        context, connection->client_id(), std::make_unique<magma_system_command_buffer>(*cmd_buf));

    if (!command_buffer->InitializeResources(std::move(resources), std::move(wait_semaphores),
                                             std::move(signal_semaphores))) {
      return DRETP(nullptr, "Failed to initialize resources");
    }
    batch = std::move(command_buffer);
  } else {
    batch = std::make_unique<EventBatch>(context, std::move(wait_semaphores),
                                         std::move(signal_semaphores));
  }

  return batch;
}

magma::Status MsdVslContext::SubmitBatch(std::unique_ptr<MappedBatch> batch) {
  auto connection = connection_.lock();
  if (!connection) {
    DMESSAGE("Can't submit without connection");
    return MAGMA_STATUS_OK;
  }

  std::shared_ptr<MsdVslContext> context = batch->GetContext().lock();
  DASSERT(context.get() == static_cast<MsdVslContext*>(this));

  // If there are any mappings pending release, submit them now.
  connection->SubmitPendingReleaseMappings(context);

  // TODO(fxb/42748): handle wait semaphores.
  return connection->SubmitBatch(std::move(batch));
}

bool MsdVslContext::MapRingbuffer(Ringbuffer* ringbuffer) {
  uint64_t gpu_addr;
  if (exec_address_space()->GetRingbufferGpuAddress(&gpu_addr)) {
    // Already mapped.
    return true;
  }
  bool res = ringbuffer->MultiMap(exec_address_space(), kRingbufferGpuAddr);
  if (res) {
    exec_address_space()->SetRingbufferGpuAddress(kRingbufferGpuAddr);
  }
  return res;
}

void msd_context_destroy(msd_context_t* abi_context) { delete MsdVslAbiContext::cast(abi_context); }

magma_status_t msd_context_execute_immediate_commands(msd_context_t* ctx, uint64_t commands_size,
                                                      void* commands, uint64_t semaphore_count,
                                                      msd_semaphore_t** msd_semaphores) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t msd_context_execute_command_buffer_with_resources(
    struct msd_context_t* ctx, struct magma_system_command_buffer* cmd_buf,
    struct magma_system_exec_resource* exec_resources, struct msd_buffer_t** buffers,
    struct msd_semaphore_t** wait_semaphores, struct msd_semaphore_t** signal_semaphores) {
  auto context = MsdVslAbiContext::cast(ctx)->ptr();

  std::unique_ptr<MappedBatch> batch = MsdVslContext::CreateBatch(
      context, cmd_buf, exec_resources, buffers, wait_semaphores, signal_semaphores);
  if (batch->IsCommandBuffer()) {
    auto* command_buffer = static_cast<CommandBuffer*>(batch.get());
    if (!command_buffer->PrepareForExecution()) {
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                      "Failed to prepare command buffer for execution");
    }
  }
  magma::Status status = context->SubmitBatch(std::move(batch));
  return status.get();
}
