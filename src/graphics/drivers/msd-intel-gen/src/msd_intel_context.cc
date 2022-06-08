// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_context.h"

#include "address_space.h"
#include "command_buffer.h"
#include "magma_intel_gen_defs.h"
#include "msd_intel_connection.h"
#include "platform_logger.h"
#include "platform_thread.h"
#include "platform_trace.h"

// static
void MsdIntelContext::HandleWaitContext::Starter(void* context, void* cancel_token) {
  reinterpret_cast<HandleWaitContext*>(context)->cancel_token = cancel_token;
}

// static
void MsdIntelContext::HandleWaitContext::Completer(void* context, magma_status_t status,
                                                   magma_handle_t handle) {
  // Ensure handle is closed.
  auto semaphore = magma::PlatformSemaphore::Import(handle);

  auto wait_context =
      std::unique_ptr<HandleWaitContext>(reinterpret_cast<HandleWaitContext*>(context));

  // Starter must have been called first.
  DASSERT(wait_context->cancel_token);

  if (wait_context->completed)
    return;

  semaphore->Reset();

  // Complete the wait if the context is not shutdown and this wasn't already completed
  // (see ::UpdateWaitSet())
  if (wait_context->context)
    wait_context->context->WaitComplete(wait_context.get(), status);
}

std::vector<std::shared_ptr<magma::PlatformSemaphore>> MsdIntelContext::GetWaitSemaphores() const {
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> semaphores;
  for (auto& wait_context : wait_set_) {
    semaphores.push_back(wait_context->semaphore);
  }
  return semaphores;
}

void MsdIntelContext::SetEngineState(EngineCommandStreamerId id,
                                     std::unique_ptr<MsdIntelBuffer> context_buffer,
                                     std::unique_ptr<Ringbuffer> ringbuffer) {
  DASSERT(context_buffer);
  DASSERT(ringbuffer);

  auto iter = state_map_.find(id);
  DASSERT(iter == state_map_.end());

  state_map_[id] = PerEngineState{std::move(context_buffer), nullptr, std::move(ringbuffer), {}};
}

bool MsdIntelContext::Map(std::shared_ptr<AddressSpace> address_space, EngineCommandStreamerId id) {
  auto iter = state_map_.find(id);
  if (iter == state_map_.end())
    return DRETF(false, "couldn't find engine command streamer");

  DLOG("Mapping context for engine %d", id);

  PerEngineState& state = iter->second;

  if (state.context_mapping) {
    if (state.context_mapping->address_space().lock() == address_space)
      return true;
    return DRETF(false, "already mapped to a different address space");
  }

  state.context_mapping = AddressSpace::MapBufferGpu(address_space, state.context_buffer);
  if (!state.context_mapping)
    return DRETF(false, "context map failed");

  if (!state.ringbuffer->Map(address_space, &state.ringbuffer_gpu_addr)) {
    state.context_mapping.reset();
    return DRETF(false, "ringbuffer map failed");
  }

  return true;
}

bool MsdIntelContext::Unmap(EngineCommandStreamerId id) {
  auto iter = state_map_.find(id);
  if (iter == state_map_.end())
    return DRETF(false, "couldn't find engine command streamer");

  DLOG("Unmapping context for engine %d", id);

  PerEngineState& state = iter->second;

  if (!state.context_mapping)
    return DRETF(false, "context not mapped");

  state.context_mapping.reset();

  if (!state.ringbuffer->Unmap())
    return DRETF(false, "ringbuffer unmap failed");

  return true;
}

bool MsdIntelContext::GetGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out) {
  auto iter = state_map_.find(id);
  if (iter == state_map_.end())
    return DRETF(false, "couldn't find engine command streamer");

  PerEngineState& state = iter->second;
  if (!state.context_mapping)
    return DRETF(false, "context not mapped");

  *addr_out = state.context_mapping->gpu_addr();
  return true;
}

bool MsdIntelContext::GetRingbufferGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out) {
  auto iter = state_map_.find(id);
  if (iter == state_map_.end())
    return DRETF(false, "couldn't find engine command streamer");

  PerEngineState& state = iter->second;
  if (!state.context_mapping)
    return DRETF(false, "context not mapped");

  *addr_out = state.ringbuffer_gpu_addr;

  return true;
}

void MsdIntelContext::Shutdown() {
  auto connection = connection_.lock();

  for (auto& wait_context : wait_set_) {
    if (connection && wait_context->cancel_token) {
      connection->CancelHandleWait(wait_context->cancel_token);
    }
    wait_context->context = nullptr;
  }

  // Clear presubmit command buffers so buffer release doesn't see stuck mappings
  while (presubmit_queue_.size()) {
    presubmit_queue_.pop();
  }
}

magma::Status MsdIntelContext::SubmitCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer) {
  TRACE_DURATION("magma", "SubmitCommandBuffer");
  uint64_t ATTRIBUTE_UNUSED buffer_id = command_buffer->GetBatchBufferId();
  TRACE_FLOW_STEP("magma", "command_buffer", buffer_id);

  // Keep track of which command streamers are used by this context.
  SetTargetCommandStreamer(command_buffer->get_command_streamer());

  if (killed())
    return DRET(MAGMA_STATUS_CONTEXT_KILLED);

  return SubmitBatch(std::move(command_buffer));
}

magma::Status MsdIntelContext::SubmitBatch(std::unique_ptr<MappedBatch> batch) {
  presubmit_queue_.push(std::move(batch));

  if (presubmit_queue_.size() == 1)
    return ProcessPresubmitQueue();

  return MAGMA_STATUS_OK;
}

void MsdIntelContext::WaitComplete(HandleWaitContext* wait_context, magma_status_t status) {
  DLOG("WaitComplete semaphore %lu status %d", wait_context->semaphore->id(), status);

  if (status != MAGMA_STATUS_OK) {
    DMESSAGE("Wait complete failed: %d", status);
    // Just return as the connection is probably shutting down.
    return;
  }

  for (auto iter = wait_set_.begin(); iter != wait_set_.end(); iter++) {
    if (wait_context == *iter) {
      wait_context->completed = true;
      wait_set_.erase(iter);

      wait_context = nullptr;
      break;
    }
  }

  if (wait_context) {
    // Not found; assume it was handled in UpdateWaitSet()
    return;
  }

  // If all semaphores in the wait set have completed, submit the batch.
  if (wait_set_.empty()) {
    ProcessPresubmitQueue();
  }
}

// Used by the connection for stalling on buffer release.
void MsdIntelContext::UpdateWaitSet() {
  for (auto iter = wait_set_.begin(); iter != wait_set_.end();) {
    HandleWaitContext* wait_context = *iter;
    if ((*iter)->semaphore->Wait(0)) {
      // Semaphore was reset; now mark this context to be skipped when the async completer
      // callback happens
      wait_context->completed = true;

      iter = wait_set_.erase(iter);
    } else {
      iter++;
    }
  }

  // If all semaphores in the wait set have completed, submit the batch.
  if (wait_set_.empty()) {
    ProcessPresubmitQueue();
  }
}

magma::Status MsdIntelContext::ProcessPresubmitQueue() {
  DASSERT(wait_set_.empty());

  while (presubmit_queue_.size()) {
    DLOG("presubmit_queue_ size %zu", presubmit_queue_.size());

    std::vector<std::shared_ptr<magma::PlatformSemaphore>> semaphores;

    auto& batch = presubmit_queue_.front();

    if (batch->GetType() == MappedBatch::BatchType::COMMAND_BUFFER) {
      // Takes ownership
      semaphores = static_cast<CommandBuffer*>(batch.get())->wait_semaphores();
    }

    auto connection = connection_.lock();
    if (!connection)
      return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "couldn't lock reference to connection");

    if (killed())
      return DRET(MAGMA_STATUS_CONTEXT_KILLED);

    if (semaphores.size() == 0) {
      DLOG("queue head has no semaphores, submitting");

      if (batch->GetType() == MappedBatch::BatchType::COMMAND_BUFFER) {
        TRACE_DURATION("magma", "SubmitBatchLocked");
        uint64_t ATTRIBUTE_UNUSED buffer_id =
            static_cast<CommandBuffer*>(batch.get())->GetBatchBufferId();
        TRACE_FLOW_STEP("magma", "command_buffer", buffer_id);
      }

      connection->SubmitBatch(std::move(batch));
      presubmit_queue_.pop();

    } else {
      DLOG("adding waitset with %zu semaphores", semaphores.size());

      for (auto& semaphore : semaphores) {
        AddToWaitset(connection, std::move(semaphore));
      }

      break;
    }
  }

  return MAGMA_STATUS_OK;
}

void MsdIntelContext::AddToWaitset(std::shared_ptr<MsdIntelConnection> connection,
                                   std::shared_ptr<magma::PlatformSemaphore> semaphore) {
  magma_handle_t handle;
  bool result = semaphore->duplicate_handle(&handle);
  if (!result) {
    DASSERT(false);
    return;
  }

  auto wait_context = std::make_unique<HandleWaitContext>(this, semaphore);

  wait_set_.push_back(wait_context.get());

  connection->AddHandleWait(HandleWaitContext::Completer, HandleWaitContext::Starter,
                            wait_context.release(), handle);
}

void MsdIntelContext::Kill() {
  if (killed_)
    return;
  killed_ = true;
  auto connection = connection_.lock();
  if (connection)
    connection->SendContextKilled();
}

//////////////////////////////////////////////////////////////////////////////

void msd_context_destroy(msd_context_t* ctx) {
  auto abi_context = MsdIntelAbiContext::cast(ctx);
  // get a copy of the shared ptr
  auto client_context = abi_context->ptr();
  // delete the abi container
  delete abi_context;
  // can safely unmap contexts only from the device thread; for that we go through the connection
  auto connection = client_context->connection().lock();
  DASSERT(connection);
  connection->DestroyContext(std::move(client_context));
}

magma_status_t msd_context_execute_immediate_commands(msd_context_t* ctx, uint64_t commands_size,
                                                      void* commands, uint64_t semaphore_count,
                                                      msd_semaphore_t** msd_semaphores) {
  return MAGMA_STATUS_CONTEXT_KILLED;
}

magma_status_t msd_context_execute_command_buffer_with_resources(
    msd_context_t* ctx, magma_command_buffer* cmd_buf, magma_exec_resource* exec_resources,
    msd_buffer_t** buffers, msd_semaphore_t** wait_semaphores,
    msd_semaphore_t** signal_semaphores) {
  auto context = MsdIntelAbiContext::cast(ctx)->ptr();

  auto command_buffer = CommandBuffer::Create(context, cmd_buf, exec_resources, buffers,
                                              wait_semaphores, signal_semaphores);
  if (!command_buffer)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Failed to create command buffer");

  TRACE_DURATION_BEGIN("magma", "PrepareForExecution", "id", command_buffer->GetBatchBufferId());
  if (!command_buffer->PrepareForExecution())
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to prepare command buffer for execution");
  TRACE_DURATION_END("magma", "PrepareForExecution");

  magma::Status status = context->SubmitCommandBuffer(std::move(command_buffer));
  return status.get();
}
