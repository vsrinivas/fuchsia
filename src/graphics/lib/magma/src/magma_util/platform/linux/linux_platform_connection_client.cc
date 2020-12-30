// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_platform_connection_client.h"

#include <vector>

#include "magma_util/macros.h"

namespace magma {

magma_status_t LinuxPlatformConnectionClient::ImportBuffer(PlatformBuffer* buffer) {
  if (!buffer)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "attempting to import null buffer");

  uint32_t handle;
  if (!buffer->duplicate_handle(&handle))
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to get duplicate_handle");

  uint64_t buffer_id;
  if (!delegate_->ImportBuffer(handle, &buffer_id))
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "delegate failed ImportBuffer");

  return MAGMA_STATUS_OK;
}

magma_status_t LinuxPlatformConnectionClient::ReleaseBuffer(uint64_t buffer_id) {
  if (!delegate_->ReleaseBuffer(buffer_id))
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "delegate failed ReleaseBuffer");

  return MAGMA_STATUS_OK;
}

magma_status_t LinuxPlatformConnectionClient::ImportObject(uint32_t handle,
                                                           PlatformObject::Type object_type) {
  return delegate_->ImportObject(handle, object_type) ? MAGMA_STATUS_OK
                                                      : MAGMA_STATUS_INTERNAL_ERROR;
}

magma_status_t LinuxPlatformConnectionClient::ReleaseObject(uint64_t object_id,
                                                            PlatformObject::Type object_type) {
  return delegate_->ReleaseObject(object_id, object_type) ? MAGMA_STATUS_OK
                                                          : MAGMA_STATUS_INTERNAL_ERROR;
}

void LinuxPlatformConnectionClient::CreateContext(uint32_t* context_id_out) {
  auto context_id = next_context_id_++;
  *context_id_out = context_id;
  if (!delegate_->CreateContext(context_id)) {
    error_ = MAGMA_STATUS_INTERNAL_ERROR;
  }
}

void LinuxPlatformConnectionClient::DestroyContext(uint32_t context_id) {
  if (!delegate_->DestroyContext(context_id)) {
    error_ = MAGMA_STATUS_INTERNAL_ERROR;
  }
}

void LinuxPlatformConnectionClient::SetError(magma_status_t error) {
  if (error_ == MAGMA_STATUS_OK) {
    error_ = DRET_MSG(error, "ZirconPlatformConnection encountered dispatcher error");
  }
}

magma_status_t LinuxPlatformConnectionClient::GetError() {
  magma_status_t error = error_;
  error_ = MAGMA_STATUS_OK;
  return error;
}

magma_status_t LinuxPlatformConnectionClient::MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va,
                                                           uint64_t page_offset,
                                                           uint64_t page_count, uint64_t flags) {
  if (!delegate_->MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, flags)) {
    SetError(MAGMA_STATUS_INVALID_ARGS);
  }
  return MAGMA_STATUS_OK;
}

magma_status_t LinuxPlatformConnectionClient::UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) {
  if (!delegate_->UnmapBufferGpu(buffer_id, gpu_va)) {
    SetError(MAGMA_STATUS_INVALID_ARGS);
  }
  return MAGMA_STATUS_OK;
}

magma_status_t LinuxPlatformConnectionClient::CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                                           uint64_t page_count) {
  if (!delegate_->CommitBuffer(buffer_id, page_offset, page_count)) {
    SetError(MAGMA_STATUS_INVALID_ARGS);
  }
  return MAGMA_STATUS_OK;
}

magma_status_t LinuxPlatformConnectionClient::ReadNotificationChannel(void* buffer,
                                                                      size_t buffer_size,
                                                                      size_t* buffer_size_out) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}

void LinuxPlatformConnectionClient::ExecuteCommandBufferWithResources(
    uint32_t context_id, magma_system_command_buffer* command_buffer,
    magma_system_exec_resource* resources, uint64_t* semaphores) {
  std::vector<magma_system_exec_resource> resource_array;
  resource_array.reserve(command_buffer->resource_count);

  for (uint32_t i = 0; i < command_buffer->resource_count; i++) {
    resource_array.push_back(resources[i]);
  }

  std::vector<uint64_t> semaphore_array;
  semaphore_array.reserve(command_buffer->wait_semaphore_count +
                          command_buffer->signal_semaphore_count);

  for (uint32_t i = 0;
       i < command_buffer->wait_semaphore_count + command_buffer->signal_semaphore_count; i++) {
    semaphore_array.push_back(semaphores[i]);
  }

  auto command_buffer_ptr = std::make_unique<magma_system_command_buffer>();
  *command_buffer_ptr = *command_buffer;

  magma::Status status = delegate_->ExecuteCommandBufferWithResources(
      context_id, std::move(command_buffer_ptr), std::move(resource_array),
      std::move(semaphore_array));

  if (!status.ok()) {
    DMESSAGE("ExecuteCommandBufferWithResources failed: %d", status.get());
    SetError(status.get());
  }
}

void LinuxPlatformConnectionClient::ExecuteImmediateCommands(
    uint32_t context_id, uint64_t command_count, magma_inline_command_buffer* command_buffers) {
  DMESSAGE("ExecuteImmediateCommands not implemented");
}

std::unique_ptr<PlatformConnectionClient> PlatformConnectionClient::Create(
    uint32_t device_handle, uint32_t device_notification_handle) {
  return DRETP(nullptr, "Not implemented");
}

}  // namespace magma
