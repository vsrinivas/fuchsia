// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"

#include <chrono>

#include "magma_util/macros.h"
#include "platform_connection_client.h"
#include "platform_device_client.h"
#include "platform_handle.h"
#include "platform_logger.h"
#include "platform_port.h"
#include "platform_semaphore.h"
#include "platform_thread.h"
#include "platform_trace.h"
#include "platform_trace_provider.h"

magma_status_t magma_device_import(uint32_t device_handle, magma_device_t* device) {
  auto platform_device_client = magma::PlatformDeviceClient::Create(device_handle);
  if (!platform_device_client) {
    return DRET(MAGMA_STATUS_INTERNAL_ERROR);
  }
  *device = reinterpret_cast<magma_device_t>(platform_device_client.release());
  return MAGMA_STATUS_OK;
}

void magma_device_release(magma_device_t device) {
  delete reinterpret_cast<magma::PlatformDeviceClient*>(device);
}

magma_status_t magma_query2(magma_device_t device, uint64_t id, uint64_t* value_out) {
  if (!value_out)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "bad value_out address");

  if (id == MAGMA_QUERY_MINIMUM_MAPPABLE_ADDRESS) {
    *value_out = magma::PlatformBuffer::MappingAddressRange::CreateDefault()->Base();
    return MAGMA_STATUS_OK;
  }

  auto platform_device_client = reinterpret_cast<magma::PlatformDeviceClient*>(device);

  if (!platform_device_client->Query(id, value_out))
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "magma::PlatformDeviceClient::Query failed");

  DLOG("magma_query2 id %" PRIu64 " returned 0x%" PRIx64, id, *value_out);
  return MAGMA_STATUS_OK;
}

magma_status_t magma_query_returns_buffer2(magma_device_t device, uint64_t id,
                                           uint32_t* handle_out) {
  auto platform_device_client = reinterpret_cast<magma::PlatformDeviceClient*>(device);
  if (!handle_out)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "bad handle_out address");

  if (!platform_device_client->QueryReturnsBuffer(id, handle_out))
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                    "magma::PlatformDeviceClient::QueryReturnsBuffer failed");

  DLOG("magma_query_returns_buffer2 id %" PRIu64 " returned buffer 0x%x", id, *handle_out);
  return MAGMA_STATUS_OK;
}

magma_status_t magma_create_connection2(magma_device_t device, magma_connection_t* connection_out) {
  auto platform_device_client = reinterpret_cast<magma::PlatformDeviceClient*>(device);

  auto connection = platform_device_client->Connect();
  if (!connection) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "couldn't connect");
  }

  *connection_out = connection.release();
  return MAGMA_STATUS_OK;
}

void magma_release_connection(magma_connection_t connection) {
  // TODO(MA-109): close the connection
  delete magma::PlatformConnectionClient::cast(connection);
}

magma_status_t magma_get_error(magma_connection_t connection) {
  return magma::PlatformConnectionClient::cast(connection)->GetError();
}

void magma_create_context(magma_connection_t connection, uint32_t* context_id_out) {
  magma::PlatformConnectionClient::cast(connection)->CreateContext(context_id_out);
}

void magma_release_context(magma_connection_t connection, uint32_t context_id) {
  magma::PlatformConnectionClient::cast(connection)->DestroyContext(context_id);
}

magma_status_t magma_create_buffer(magma_connection_t connection, uint64_t size, uint64_t* size_out,
                                   magma_buffer_t* buffer_out) {
  auto platform_buffer = magma::PlatformBuffer::Create(size, "magma_create_buffer");
  if (!platform_buffer)
    return DRET(MAGMA_STATUS_MEMORY_ERROR);

  magma_status_t result =
      magma::PlatformConnectionClient::cast(connection)->ImportBuffer(platform_buffer.get());
  if (result != MAGMA_STATUS_OK)
    return DRET(result);

  *size_out = platform_buffer->size();
  *buffer_out =
      reinterpret_cast<magma_buffer_t>(platform_buffer.release());  // Ownership passed across abi

  return MAGMA_STATUS_OK;
}

void magma_release_buffer(magma_connection_t connection, magma_buffer_t buffer) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
  magma::PlatformConnectionClient::cast(connection)->ReleaseBuffer(platform_buffer->id());
  delete platform_buffer;
}

magma_status_t magma_set_cache_policy(magma_buffer_t buffer, magma_cache_policy_t policy) {
  bool result = reinterpret_cast<magma::PlatformBuffer*>(buffer)->SetCachePolicy(policy);
  return result ? MAGMA_STATUS_OK : MAGMA_STATUS_INTERNAL_ERROR;
}

magma_status_t magma_set_buffer_mapping_address_range(magma_buffer_t buffer, uint32_t handle) {
  auto address_range =
      magma::PlatformBuffer::MappingAddressRange::Create(magma::PlatformHandle::Create(handle));
  if (!address_range)
    return DRET(MAGMA_STATUS_INVALID_ARGS);

  magma::Status status = reinterpret_cast<magma::PlatformBuffer*>(buffer)->SetMappingAddressRange(
      std::move(address_range));
  return status.get();
}

uint64_t magma_get_buffer_id(magma_buffer_t buffer) {
  return reinterpret_cast<magma::PlatformBuffer*>(buffer)->id();
}

uint64_t magma_get_buffer_size(magma_buffer_t buffer) {
  return reinterpret_cast<magma::PlatformBuffer*>(buffer)->size();
}

magma_status_t magma_duplicate_handle(uint32_t buffer_handle, uint32_t* buffer_handle_out) {
  if (!magma::PlatformHandle::duplicate_handle(buffer_handle, buffer_handle_out))
    return DRET(MAGMA_STATUS_INTERNAL_ERROR);
  return MAGMA_STATUS_OK;
}

magma_status_t magma_release_buffer_handle(uint32_t buffer_handle) {
  if (!magma::PlatformHandle::Create(buffer_handle)) {
    return DRET(MAGMA_STATUS_INTERNAL_ERROR);
  }
  return MAGMA_STATUS_OK;
}

uint32_t magma_get_notification_channel_handle(magma_connection_t connection) {
  return magma::PlatformConnectionClient::cast(connection)->GetNotificationChannelHandle();
}

magma_status_t magma_wait_notification_channel(magma_connection_t connection, int64_t timeout_ns) {
  return magma::PlatformConnectionClient::cast(connection)->WaitNotificationChannel(timeout_ns);
}

magma_status_t magma_read_notification_channel(magma_connection_t connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out) {
  return magma::PlatformConnectionClient::cast(connection)
      ->ReadNotificationChannel(buffer, buffer_size, buffer_size_out);
}

magma_status_t magma_clean_cache(magma_buffer_t buffer, uint64_t offset, uint64_t size,
                                 magma_cache_operation_t operation) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
  bool invalidate;
  switch (operation) {
    case MAGMA_CACHE_OPERATION_CLEAN:
      invalidate = false;
      break;
    case MAGMA_CACHE_OPERATION_CLEAN_INVALIDATE:
      invalidate = true;
      break;
    default:
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "invalid cache operations");
  }

  bool result = platform_buffer->CleanCache(offset, size, invalidate);
  return result ? MAGMA_STATUS_OK : MAGMA_STATUS_INTERNAL_ERROR;
}

magma_status_t magma_import(magma_connection_t connection, uint32_t buffer_handle,
                            magma_buffer_t* buffer_out) {
  auto platform_buffer = magma::PlatformBuffer::Import(buffer_handle);
  if (!platform_buffer)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "PlatformBuffer::Import failed");

  magma_status_t result =
      magma::PlatformConnectionClient::cast(connection)->ImportBuffer(platform_buffer.get());
  if (result != MAGMA_STATUS_OK)
    return DRET_MSG(result, "ImportBuffer failed");

  *buffer_out = reinterpret_cast<magma_buffer_t>(platform_buffer.release());

  return MAGMA_STATUS_OK;
}

magma_status_t magma_export(magma_connection_t connection, magma_buffer_t buffer,
                            uint32_t* buffer_handle_out) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

  if (!platform_buffer->duplicate_handle(buffer_handle_out))
    return DRET(MAGMA_STATUS_INVALID_ARGS);

  return MAGMA_STATUS_OK;
}

magma_status_t magma_map(magma_connection_t connection, magma_buffer_t buffer, void** addr_out) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

  if (!platform_buffer->MapCpu(addr_out))
    return DRET(MAGMA_STATUS_MEMORY_ERROR);

  return MAGMA_STATUS_OK;
}

magma_status_t magma_map_aligned(magma_connection_t connection, magma_buffer_t buffer,
                                 uint64_t alignment, void** addr_out) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

  if (!platform_buffer->MapCpu(addr_out, alignment))
    return DRET(MAGMA_STATUS_MEMORY_ERROR);

  return MAGMA_STATUS_OK;
}

magma_status_t magma_map_specific(magma_connection_t connection, magma_buffer_t buffer,
                                  uint64_t addr, uint64_t offset, uint64_t length) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

  // This may happen often if there happens to be another allocation already there, so don't DRET
  if (!platform_buffer->MapAtCpuAddr(addr, offset, length))
    return MAGMA_STATUS_MEMORY_ERROR;

  return MAGMA_STATUS_OK;
}

magma_status_t magma_unmap(magma_connection_t connection, magma_buffer_t buffer) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);

  if (!platform_buffer->UnmapCpu())
    return DRET(MAGMA_STATUS_MEMORY_ERROR);

  return MAGMA_STATUS_OK;
}

void magma_map_buffer_gpu(magma_connection_t connection, magma_buffer_t buffer,
                          uint64_t page_offset, uint64_t page_count, uint64_t gpu_va,
                          uint64_t map_flags) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
  uint64_t buffer_id = platform_buffer->id();
  magma::PlatformConnectionClient::cast(connection)
      ->MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, map_flags);
}

magma_status_t magma_get_buffer_cache_policy(magma_buffer_t buffer,
                                             magma_cache_policy_t* cache_policy_out) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
  return platform_buffer->GetCachePolicy(cache_policy_out);
}

magma_status_t magma_get_buffer_is_mappable(magma_buffer_t buffer, uint32_t flags,
                                            magma_bool_t* is_mappable_out) {
  if (flags) {
    return DRET(MAGMA_STATUS_INVALID_ARGS);
  }
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
  return platform_buffer->GetIsMappable(is_mappable_out);
}

void magma_unmap_buffer_gpu(magma_connection_t connection, magma_buffer_t buffer, uint64_t gpu_va) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
  uint64_t buffer_id = platform_buffer->id();
  magma::PlatformConnectionClient::cast(connection)->UnmapBufferGpu(buffer_id, gpu_va);
}

magma_status_t magma_commit_buffer(magma_connection_t connection, magma_buffer_t buffer,
                                   uint64_t page_offset, uint64_t page_count) {
  auto platform_buffer = reinterpret_cast<magma::PlatformBuffer*>(buffer);
  uint64_t buffer_id = platform_buffer->id();
  if (!platform_buffer->CommitPages(page_offset, page_count))
    return DRET(MAGMA_STATUS_MEMORY_ERROR);
  magma::PlatformConnectionClient::cast(connection)
      ->CommitBuffer(buffer_id, page_offset, page_count);
  return MAGMA_STATUS_OK;
}

void magma_execute_command_buffer_with_resources(magma_connection_t connection, uint32_t context_id,
                                                 struct magma_system_command_buffer* command_buffer,
                                                 struct magma_system_exec_resource* resources,
                                                 uint64_t* semaphore_ids) {
  DASSERT(command_buffer->batch_buffer_resource_index < command_buffer->num_resources);

  uint64_t ATTRIBUTE_UNUSED id = resources[command_buffer->batch_buffer_resource_index].buffer_id;
  TRACE_FLOW_BEGIN("magma", "command_buffer", id);

  magma::PlatformConnectionClient::cast(connection)
      ->ExecuteCommandBufferWithResources(context_id, command_buffer, resources, semaphore_ids);
}

void magma_execute_immediate_commands2(magma_connection_t connection, uint32_t context_id,
                                       uint64_t command_count,
                                       magma_inline_command_buffer* command_buffers) {
  magma::PlatformConnectionClient::cast(connection)
      ->ExecuteImmediateCommands(context_id, command_count, command_buffers);
}

magma_status_t magma_create_semaphore(magma_connection_t connection,
                                      magma_semaphore_t* semaphore_out) {
  auto semaphore = magma::PlatformSemaphore::Create();
  if (!semaphore)
    return MAGMA_STATUS_MEMORY_ERROR;

  uint32_t handle;
  if (!semaphore->duplicate_handle(&handle))
    return DRET_MSG(MAGMA_STATUS_ACCESS_DENIED, "failed to duplicate handle");

  magma_status_t result = magma::PlatformConnectionClient::cast(connection)
                              ->ImportObject(handle, magma::PlatformObject::SEMAPHORE);
  if (result != MAGMA_STATUS_OK)
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to ImportObject");

  *semaphore_out = reinterpret_cast<magma_semaphore_t>(semaphore.release());
  return MAGMA_STATUS_OK;
}

void magma_release_semaphore(magma_connection_t connection, magma_semaphore_t semaphore) {
  auto platform_semaphore = reinterpret_cast<magma::PlatformSemaphore*>(semaphore);
  magma::PlatformConnectionClient::cast(connection)
      ->ReleaseObject(platform_semaphore->id(), magma::PlatformObject::SEMAPHORE);
  delete platform_semaphore;
}

uint64_t magma_get_semaphore_id(magma_semaphore_t semaphore) {
  return reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->id();
}

void magma_signal_semaphore(magma_semaphore_t semaphore) {
  reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->Signal();
}

void magma_reset_semaphore(magma_semaphore_t semaphore) {
  reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->Reset();
}

magma_status_t magma_wait_semaphores(const magma_semaphore_t* semaphores, uint32_t count,
                                     uint64_t timeout_ms, magma_bool_t wait_all) {
  if (count == 1) {
    if (!reinterpret_cast<magma::PlatformSemaphore*>(semaphores[0])->WaitNoReset(timeout_ms))
      return MAGMA_STATUS_TIMED_OUT;
    return MAGMA_STATUS_OK;
  }

  std::unique_ptr<magma::PlatformPort> port = magma::PlatformPort::Create();
  for (uint32_t i = 0; i < count; i++) {
    if (!reinterpret_cast<magma::PlatformSemaphore*>(semaphores[i])->WaitAsync(port.get()))
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "WaitAsync failed");
  }

  if (!wait_all) {
    uint64_t key;
    return port->Wait(&key, timeout_ms).get();
  }

  auto end_time = timeout_ms == UINT64_MAX
                      ? std::chrono::steady_clock::time_point::max()
                      : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  for (uint32_t i = 0; i < count; i++) {
    uint64_t key;
    auto time_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - std::chrono::steady_clock::now());
    magma::Status status =
        port->Wait(&key, time_remaining.count() > 0 ? time_remaining.count() : 0);
    if (!status)
      return status.get();
  }
  return MAGMA_STATUS_OK;
}

magma_status_t magma_export_semaphore(magma_connection_t connection, magma_semaphore_t semaphore,
                                      uint32_t* semaphore_handle_out) {
  auto platform_semaphore = reinterpret_cast<magma::PlatformSemaphore*>(semaphore);

  if (!platform_semaphore->duplicate_handle(semaphore_handle_out))
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "duplicate_handle failed");

  return MAGMA_STATUS_OK;
}

magma_status_t magma_import_semaphore(magma_connection_t connection, uint32_t semaphore_handle,
                                      magma_semaphore_t* semaphore_out) {
  auto platform_semaphore = magma::PlatformSemaphore::Import(semaphore_handle);
  if (!platform_semaphore)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "PlatformSemaphore::Import failed");

  uint32_t handle;
  if (!platform_semaphore->duplicate_handle(&handle))
    return DRET_MSG(MAGMA_STATUS_ACCESS_DENIED, "failed to duplicate handle");

  magma_status_t result = magma::PlatformConnectionClient::cast(connection)
                              ->ImportObject(handle, magma::PlatformObject::SEMAPHORE);
  if (result != MAGMA_STATUS_OK)
    return DRET_MSG(result, "ImportObject failed: %d", result);

  *semaphore_out = reinterpret_cast<magma_semaphore_t>(platform_semaphore.release());

  return MAGMA_STATUS_OK;
}

magma_status_t magma_initialize_tracing(magma_handle_t channel) {
  if (!channel)
    return MAGMA_STATUS_INVALID_ARGS;
  if (magma::PlatformTraceProvider::Get()) {
    if (magma::PlatformTraceProvider::Get()->IsInitialized())
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Shouldn't initialize tracing twice");
    if (!magma::PlatformTraceProvider::Get()->Initialize(channel))
      return DRET(MAGMA_STATUS_INTERNAL_ERROR);
  } else {
    // Close channel.
    magma::PlatformHandle::Create(channel);
  }
  return MAGMA_STATUS_OK;
}

magma_status_t magma_initialize_logging(magma_handle_t channel) {
  if (!channel)
    return MAGMA_STATUS_INVALID_ARGS;

  auto handle = magma::PlatformHandle::Create(channel);
  if (magma::PlatformLogger::IsInitialized())
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Shouldn't initialize logging twice");

  if (!magma::PlatformLogger::Initialize(std::move(handle)))
    return MAGMA_STATUS_INTERNAL_ERROR;

  return MAGMA_STATUS_OK;
}
