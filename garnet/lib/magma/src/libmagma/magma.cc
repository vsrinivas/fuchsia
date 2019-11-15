// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"

#include <chrono>
#include <vector>

#include "magma_sysmem.h"
#include "magma_util/macros.h"
#include "platform_connection.h"
#include "platform_connection_client.h"
#include "platform_device_client.h"
#include "platform_handle.h"
#include "platform_port.h"
#include "platform_semaphore.h"
#include "platform_sysmem_connection.h"
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
  auto platform_device_client = reinterpret_cast<magma::PlatformDeviceClient*>(device);
  if (!value_out)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "bad value_out address");

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

// TODO(fxb/13095): Remove.
magma_status_t magma_create_connection(int32_t file_descriptor,
                                       magma_connection_t* connection_out) {
  uint32_t primary_channel;
  uint32_t notification_channel;
  if (!magma::PlatformConnectionClient::GetHandles(file_descriptor, &primary_channel,
                                                   &notification_channel))
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "couldn't get handles from file_descriptor %d",
                    file_descriptor);

  *connection_out =
      magma::PlatformConnectionClient::Create(primary_channel, notification_channel).release();
  return MAGMA_STATUS_OK;
}

void magma_release_connection(magma_connection_t connection) {
  // TODO(MA-109): close the connection
  delete magma::PlatformConnectionClient::cast(connection);
}

magma_status_t magma_get_error(magma_connection_t connection) {
  return magma::PlatformConnectionClient::cast(connection)->GetError();
}

// TODO(fxb/13095): Remove.
magma_status_t magma_query(int fd, uint64_t id, uint64_t* value_out) {
  if (!value_out)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "bad value_out address");

  if (!magma::PlatformConnectionClient::Query(fd, id, value_out))
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "magma::PlatformConnectionClient::Query failed");

  DLOG("magma_query id %" PRIu64 " returned 0x%" PRIx64, id, *value_out);
  return MAGMA_STATUS_OK;
}

// TODO(fxb/13095): Remove.
magma_status_t magma_query_returns_buffer(int fd, uint64_t id, uint32_t* result_out) {
  if (!result_out)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "bad result_out address");

  if (!magma::PlatformConnectionClient::QueryReturnsBuffer(fd, id, result_out))
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                    "magma::PlatformConnectionClient::QueryReturnsBuffer failed");

  DLOG("magma_query_returns_buffer id %" PRIu64 " returned buffer 0x%x", id, *result_out);
  return MAGMA_STATUS_OK;
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

  if (!platform_buffer->MapAtCpuAddr(addr, offset, length))
    return DRET(MAGMA_STATUS_MEMORY_ERROR);

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

magma_status_t magma_sysmem_connection_create(magma_sysmem_connection_t* connection_out) {
  auto platform_connection = magma_sysmem::PlatformSysmemConnection::Create();
  if (!platform_connection) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to create sysmem connection");
  }
  *connection_out = reinterpret_cast<magma_sysmem_connection_t>(platform_connection.release());
  return MAGMA_STATUS_OK;
}

magma_status_t magma_sysmem_connection_import(magma_handle_t channel,
                                              magma_sysmem_connection_t* connection_out) {
  auto platform_connection = magma_sysmem::PlatformSysmemConnection::Import(channel);
  if (!platform_connection) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to create sysmem connection");
  }
  *connection_out = reinterpret_cast<magma_sysmem_connection_t>(platform_connection.release());
  return MAGMA_STATUS_OK;
}

void magma_sysmem_connection_release(magma_sysmem_connection_t connection) {
  delete reinterpret_cast<magma_sysmem::PlatformSysmemConnection*>(connection);
}

magma_status_t magma_sysmem_allocate_buffer(magma_sysmem_connection_t connection, uint32_t flags,
                                            uint64_t size, uint32_t* buffer_handle_out) {
  std::unique_ptr<magma::PlatformBuffer> buffer;
  auto sysmem_connection = reinterpret_cast<magma_sysmem::PlatformSysmemConnection*>(connection);

  magma_status_t result;
  result = sysmem_connection->AllocateBuffer(flags, size, &buffer);
  if (result != MAGMA_STATUS_OK) {
    return DRET_MSG(result, "AllocateBuffer failed: %d", result);
  }

  if (!buffer->duplicate_handle(buffer_handle_out)) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "duplicate_handle failed");
  }
  return MAGMA_STATUS_OK;
}

void magma_buffer_format_description_release(magma_buffer_format_description_t description) {
  delete reinterpret_cast<magma_sysmem::PlatformBufferDescription*>(description);
}

// |image_planes_out| must be an array with MAGMA_MAX_IMAGE_PLANES elements.
magma_status_t magma_get_buffer_format_plane_info_with_size(
    magma_buffer_format_description_t description, uint32_t width, uint32_t height,
    magma_image_plane_t* image_planes_out) {
  if (!description) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Null description");
  }
  auto buffer_description = reinterpret_cast<magma_sysmem::PlatformBufferDescription*>(description);
  if (!buffer_description->GetPlanes(width, height, image_planes_out)) {
    return DRET(MAGMA_STATUS_INVALID_ARGS);
  }
  return MAGMA_STATUS_OK;
}

magma_status_t magma_get_buffer_format_modifier(magma_buffer_format_description_t description,
                                                magma_bool_t* has_format_modifier_out,
                                                uint64_t* format_modifier_out) {
  if (!description) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Null description");
  }
  auto buffer_description = reinterpret_cast<magma_sysmem::PlatformBufferDescription*>(description);
  *has_format_modifier_out = buffer_description->has_format_modifier();
  *format_modifier_out = buffer_description->format_modifier();
  return MAGMA_STATUS_OK;
}

magma_status_t magma_get_buffer_coherency_domain(magma_buffer_format_description_t description,
                                                 uint32_t* coherency_domain_out) {
  if (!description) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Null description");
  }
  auto buffer_description = reinterpret_cast<magma_sysmem::PlatformBufferDescription*>(description);
  *coherency_domain_out = buffer_description->coherency_domain();
  return MAGMA_STATUS_OK;
}

magma_status_t magma_get_buffer_count(magma_buffer_format_description_t description,
                                      uint32_t* count_out) {
  if (!description) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Null description");
  }
  auto buffer_description = reinterpret_cast<magma_sysmem::PlatformBufferDescription*>(description);
  *count_out = buffer_description->count();
  return MAGMA_STATUS_OK;
}

magma_status_t magma_get_buffer_is_secure(magma_buffer_format_description_t description,
                                          magma_bool_t* is_secure_out) {
  if (!description) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Null description");
  }
  auto buffer_description = reinterpret_cast<magma_sysmem::PlatformBufferDescription*>(description);
  *is_secure_out = buffer_description->is_secure();
  return MAGMA_STATUS_OK;
}

magma_status_t magma_buffer_collection_import(magma_sysmem_connection_t connection, uint32_t handle,
                                              magma_buffer_collection_t* collection_out) {
  auto sysmem_connection = reinterpret_cast<magma_sysmem::PlatformSysmemConnection*>(connection);
  if (!handle) {
    magma::Status status = sysmem_connection->CreateBufferCollectionToken(&handle);
    if (!status.ok()) {
      return DRET(status.get());
    }
  }
  std::unique_ptr<magma_sysmem::PlatformBufferCollection> buffer_collection;
  magma::Status status = sysmem_connection->ImportBufferCollection(handle, &buffer_collection);
  if (!status.ok())
    return status.get();
  *collection_out = reinterpret_cast<magma_buffer_collection_t>(buffer_collection.release());
  return MAGMA_STATUS_OK;
}

void magma_buffer_collection_release(magma_sysmem_connection_t connection,
                                     magma_buffer_collection_t collection) {
  delete reinterpret_cast<magma_sysmem::PlatformBufferCollection*>(collection);
}

magma_status_t magma_buffer_constraints_create(
    magma_sysmem_connection_t connection,
    const magma_buffer_format_constraints_t* buffer_constraints_in,
    magma_sysmem_buffer_constraints_t* constraints_out) {
  auto sysmem_connection = reinterpret_cast<magma_sysmem::PlatformSysmemConnection*>(connection);
  std::unique_ptr<magma_sysmem::PlatformBufferConstraints> buffer_constraints;
  magma::Status status =
      sysmem_connection->CreateBufferConstraints(buffer_constraints_in, &buffer_constraints);
  if (!status.ok())
    return status.get();
  *constraints_out =
      reinterpret_cast<magma_sysmem_buffer_constraints_t>(buffer_constraints.release());
  return MAGMA_STATUS_OK;
}

magma_status_t magma_buffer_constraints_set_format(
    magma_sysmem_connection_t connection, magma_sysmem_buffer_constraints_t constraints,
    uint32_t index, const magma_image_format_constraints_t* format_constraints) {
  auto buffer_constraints = reinterpret_cast<magma_sysmem::PlatformBufferConstraints*>(constraints);
  return buffer_constraints->SetImageFormatConstraints(index, format_constraints).get();
}

void magma_buffer_constraints_release(magma_sysmem_connection_t connection,
                                      magma_sysmem_buffer_constraints_t constraints) {
  delete reinterpret_cast<magma_sysmem::PlatformBufferConstraints*>(constraints);
}

magma_status_t magma_buffer_collection_set_constraints(
    magma_sysmem_connection_t connection, magma_buffer_collection_t collection,
    magma_sysmem_buffer_constraints_t constraints) {
  auto buffer_collection = reinterpret_cast<magma_sysmem::PlatformBufferCollection*>(collection);
  auto buffer_constraints = reinterpret_cast<magma_sysmem::PlatformBufferConstraints*>(constraints);
  return buffer_collection->SetConstraints(buffer_constraints).get();
}

magma_status_t magma_get_buffer_format_description(
    const void* image_data, uint64_t image_data_size,
    magma_buffer_format_description_t* description_out) {
  std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
  magma_status_t status = magma_sysmem::PlatformSysmemConnection::DecodeBufferDescription(
      static_cast<const uint8_t*>(image_data), image_data_size, &description);
  if (status != MAGMA_STATUS_OK) {
    return DRET_MSG(status, "DecodePlatformBufferDescription failed: %d", status);
  }
  *description_out = reinterpret_cast<magma_buffer_format_description_t>(description.release());
  return MAGMA_STATUS_OK;
}

magma_status_t magma_sysmem_get_description_from_collection(
    magma_sysmem_connection_t connection, magma_buffer_collection_t collection,
    magma_buffer_format_description_t* buffer_format_description_out) {
  auto buffer_collection = reinterpret_cast<magma_sysmem::PlatformBufferCollection*>(collection);
  std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
  magma::Status status = buffer_collection->GetBufferDescription(&description);
  if (!status.ok()) {
    return DRET_MSG(status.get(), "GetBufferDescription failed");
  }

  *buffer_format_description_out =
      reinterpret_cast<magma_buffer_format_description_t>(description.release());
  return MAGMA_STATUS_OK;
}

magma_status_t magma_sysmem_get_buffer_handle_from_collection(magma_sysmem_connection_t connection,
                                                              magma_buffer_collection_t collection,
                                                              uint32_t index,
                                                              uint32_t* buffer_handle_out,
                                                              uint32_t* vmo_offset_out) {
  auto buffer_collection = reinterpret_cast<magma_sysmem::PlatformBufferCollection*>(collection);
  return buffer_collection->GetBufferHandle(index, buffer_handle_out, vmo_offset_out).get();
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
