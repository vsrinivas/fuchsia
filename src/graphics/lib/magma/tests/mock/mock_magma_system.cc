// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "magma.h"
#include "magma_sysmem.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "platform_semaphore.h"

std::unordered_map<uint32_t, magma::PlatformBuffer*> exported_buffers;
std::unordered_map<uint32_t, magma::PlatformSemaphore*> exported_semaphores;

class MockConnection : public magma_connection {
 public:
  uint32_t next_context_id() { return next_context_id_++; }

 private:
  uint32_t next_context_id_ = 1;
};

class MockDevice {};

magma_status_t magma_device_import(uint32_t device_handle, magma_device_t* device_out) {
  *device_out = reinterpret_cast<magma_device_t>(new MockDevice);
  return MAGMA_STATUS_OK;
}

void magma_device_release(magma_device_t device) { delete reinterpret_cast<MockDevice*>(device); }

magma_status_t magma_create_connection2(magma_device_t device, magma_connection_t* connection_out) {
  *connection_out = new MockConnection();
  return MAGMA_STATUS_OK;
}

magma_status_t magma_create_connection(int32_t fd, magma_connection_t* connection_out) {
  *connection_out = new MockConnection();
  return MAGMA_STATUS_OK;
}

void magma_release_connection(magma_connection_t connection) {
  delete static_cast<MockConnection*>(connection);
}

magma_status_t magma_get_error(magma_connection_t connection) { return MAGMA_STATUS_OK; }

magma_status_t magma_query(int32_t fd, uint64_t id, uint64_t* value_out) {
  switch (id) {
    case MAGMA_QUERY_DEVICE_ID:
      *value_out = 0x1916;
      return MAGMA_STATUS_OK;
    case MAGMA_QUERY_VENDOR_PARAM_0:
      *value_out = (23l << 32) | 8;
      return MAGMA_STATUS_OK;
    case MAGMA_QUERY_VENDOR_PARAM_0 + 1:  // gtt size
      *value_out = 1ull << 32;
      return MAGMA_STATUS_OK;
    case MAGMA_QUERY_VENDOR_PARAM_0 + 2:  // extra page count
      *value_out = 0;
      return MAGMA_STATUS_OK;
  }
  return MAGMA_STATUS_INVALID_ARGS;
}

magma_status_t magma_query2(magma_device_t device, uint64_t id, uint64_t* value_out) {
  return magma_query(-1, id, value_out);
}

magma_status_t magma_query_returns_buffer(int32_t file_descriptor, uint64_t id,
                                          uint32_t* handle_out) {
  return MAGMA_STATUS_INVALID_ARGS;
}

magma_status_t magma_query_returns_buffer2(magma_device_t device, uint64_t id,
                                           magma_handle_t* handle_out) {
  return MAGMA_STATUS_INVALID_ARGS;
}

void magma_create_context(magma_connection_t connection, uint32_t* context_id_out) {
  *context_id_out = static_cast<MockConnection*>(connection)->next_context_id();
}

void magma_release_context(magma_connection_t connection, uint32_t context_id) {}

magma_status_t magma_create_buffer(magma_connection_t connection, uint64_t size, uint64_t* size_out,
                                   magma_buffer_t* buffer_out) {
  auto buffer = magma::PlatformBuffer::Create(size, "magma-alloc");
  *buffer_out = reinterpret_cast<magma_buffer_t>(buffer.release());
  *size_out = size;
  return MAGMA_STATUS_OK;
}

void magma_release_buffer(magma_connection_t connection, magma_buffer_t buffer) {
  delete reinterpret_cast<magma::PlatformBuffer*>(buffer);
}

uint64_t magma_get_buffer_id(magma_buffer_t buffer) {
  return reinterpret_cast<magma::PlatformBuffer*>(buffer)->id();
}

uint64_t magma_get_buffer_size(magma_buffer_t buffer) {
  return reinterpret_cast<magma::PlatformBuffer*>(buffer)->size();
}

magma_status_t magma_map(magma_connection_t connection, magma_buffer_t buffer, void** addr_out) {
  if (!reinterpret_cast<magma::PlatformBuffer*>(buffer)->MapCpu(addr_out))
    return MAGMA_STATUS_MEMORY_ERROR;
  return MAGMA_STATUS_OK;
}

magma_status_t magma_buffer_set_padding(magma_connection_t connection, magma_buffer_t buffer,
                                        uint64_t padding) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_map_aligned(magma_connection_t connection, magma_buffer_t buffer,
                                 uint64_t alignment, void** addr_out) {
  if (!reinterpret_cast<magma::PlatformBuffer*>(buffer)->MapCpu(addr_out, alignment))
    return MAGMA_STATUS_MEMORY_ERROR;
  return MAGMA_STATUS_OK;
}

magma_status_t magma_unmap(magma_connection_t connection, magma_buffer_t buffer) {
  if (!reinterpret_cast<magma::PlatformBuffer*>(buffer)->UnmapCpu())
    return MAGMA_STATUS_MEMORY_ERROR;
  return MAGMA_STATUS_OK;
}

magma_status_t magma_set_cache_policy(magma_buffer_t buffer, magma_cache_policy_t policy) {
  return MAGMA_STATUS_OK;
}

magma_status_t magma_set_buffer_mapping_address_range(magma_buffer_t buffer, uint32_t handle) {
  return MAGMA_STATUS_OK;
}

magma_status_t magma_create_command_buffer(magma_connection_t connection, uint64_t size,
                                           magma_buffer_t* buffer_out) {
  uint64_t size_out;
  return magma_create_buffer(connection, size, &size_out, buffer_out);
}

void magma_release_command_buffer(magma_connection_t connection, magma_buffer_t command_buffer) {
  magma_release_buffer(connection, command_buffer);
}

void magma_submit_command_buffer(magma_connection_t connection, uint64_t command_buffer_id,
                                 uint32_t context_id) {
  DLOG("magma_system submit command buffer - STUB");
}

void magma_execute_command_buffer_with_resources(magma_connection_t connection, uint32_t context_id,
                                                 struct magma_system_command_buffer* command_buffer,
                                                 struct magma_system_exec_resource* resources,
                                                 uint64_t* semaphore_ids) {
  DLOG("magma_execute_command_buffer_with_resources - STUB");
}

void magma_execute_immediate_commands2(magma_connection_t connection, uint32_t context_id,
                                       uint64_t command_count,
                                       struct magma_inline_command_buffer* command_buffers) {}

magma_status_t magma_export(magma_connection_t connection, magma_buffer_t buffer,
                            uint32_t* buffer_handle_out) {
  uint32_t handle;
  reinterpret_cast<magma::PlatformBuffer*>(buffer)->duplicate_handle(&handle);
  exported_buffers[handle] = magma::PlatformBuffer::Import(handle).release();
  *buffer_handle_out = handle;
  return MAGMA_STATUS_OK;
}

magma_status_t magma_import(magma_connection_t connection, uint32_t buffer_handle,
                            magma_buffer_t* buffer_out) {
  *buffer_out = reinterpret_cast<magma_buffer_t>(exported_buffers[buffer_handle]);
  exported_buffers.erase(buffer_handle);
  return MAGMA_STATUS_OK;
}

magma_status_t magma_create_semaphore(magma_connection_t connection,
                                      magma_semaphore_t* semaphore_out) {
  *semaphore_out =
      reinterpret_cast<magma_semaphore_t>(magma::PlatformSemaphore::Create().release());
  return MAGMA_STATUS_OK;
}

void magma_release_semaphore(magma_connection_t connection, magma_semaphore_t semaphore) {
  delete reinterpret_cast<magma::PlatformSemaphore*>(semaphore);
}

uint64_t magma_get_semaphore_id(magma_semaphore_t semaphore) {
  return reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->id();
}

void magma_signal_semaphore(magma_semaphore_t semaphore) {}

void magma_reset_semaphore(magma_semaphore_t semaphore) {}

magma_status_t magma_wait_semaphores(const magma_semaphore_t* semaphore, uint32_t count,
                                     uint64_t timeout, magma_bool_t wait_all) {
  return MAGMA_STATUS_OK;
}

magma_status_t magma_export_semaphore(magma_connection_t connection, magma_semaphore_t semaphore,
                                      uint32_t* semaphore_handle_out) {
  uint32_t handle;
  reinterpret_cast<magma::PlatformSemaphore*>(semaphore)->duplicate_handle(&handle);
  exported_semaphores[handle] = magma::PlatformSemaphore::Import(handle).release();
  *semaphore_handle_out = handle;
  return MAGMA_STATUS_OK;
}

magma_status_t magma_import_semaphore(magma_connection_t connection, uint32_t semaphore_handle,
                                      magma_semaphore_t* semaphore_out) {
  *semaphore_out = reinterpret_cast<magma_semaphore_t>(exported_semaphores[semaphore_handle]);
  exported_semaphores.erase(semaphore_handle);
  return MAGMA_STATUS_OK;
}

void magma_map_buffer_gpu(magma_connection_t connection, magma_buffer_t buffer, uint64_t gpu_va,
                          uint64_t page_offset, uint64_t page_count, uint64_t map_flags) {}

void magma_unmap_buffer_gpu(magma_connection_t connection, magma_buffer_t buffer, uint64_t gpu_va) {
}

uint32_t magma_get_notification_channel_handle(magma_connection_t connection) { return 0; }

magma_status_t magma_read_notification_channel(magma_connection_t connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_wait_notification_channel(magma_connection_t connection, int64_t timeout_ns) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_buffer_collection_import(magma_sysmem_connection_t connection, uint32_t handle,
                                              magma_buffer_collection_t* collection_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_sysmem_connection_create(magma_sysmem_connection_t* connection_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_sysmem_connection_import(magma_handle_t channel,
                                              magma_sysmem_connection_t* connection_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}
void magma_sysmem_connection_release(magma_sysmem_connection_t connection) {}

void magma_buffer_collection_release(magma_sysmem_connection_t connection,
                                     magma_buffer_collection_t collection) {}

magma_status_t magma_buffer_constraints_create(
    magma_sysmem_connection_t connection,
    const magma_buffer_format_constraints_t* buffer_constraints,
    magma_sysmem_buffer_constraints_t* constraints_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_buffer_constraints_set_format(
    magma_sysmem_connection_t connection, magma_sysmem_buffer_constraints_t constraints,
    uint32_t index, const magma_image_format_constraints_t* format_constraints) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_buffer_constraints_set_colorspaces(
    magma_sysmem_connection_t connection, magma_sysmem_buffer_constraints_t constraints,
    uint32_t index, uint32_t color_space_count, const uint32_t* color_spaces) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

void magma_buffer_constraints_release(magma_sysmem_connection_t connection,
                                      magma_sysmem_buffer_constraints_t constraints) {}

magma_status_t magma_buffer_collection_set_constraints(
    magma_sysmem_connection_t connection, magma_buffer_collection_t collection,
    magma_sysmem_buffer_constraints_t constraints) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_coherency_domain(magma_buffer_format_description_t description,
                                                 uint32_t* coherency_domain_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_format_description(
    const void* image_data, uint64_t image_data_size,
    magma_buffer_format_description_t* description_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_format_plane_info(magma_buffer_format_description_t description,
                                                  magma_image_plane_t* image_planes_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_format_plane_info_with_size(
    magma_buffer_format_description_t description, uint32_t width, uint32_t height,
    magma_image_plane_t* image_planes_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

void magma_buffer_format_description_release(magma_buffer_format_description_t description) {}

magma_status_t magma_get_buffer_format(magma_buffer_format_description_t description,
                                       uint32_t* format_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_format_modifier(magma_buffer_format_description_t description,
                                                magma_bool_t* has_format_modifier_out,
                                                uint64_t* format_modifier_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_color_space(magma_buffer_format_description_t description,
                                            uint32_t* color_space_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_buffer_constraints_add_additional(
    magma_sysmem_connection_t connection, magma_sysmem_buffer_constraints_t constraints,
    const magma_buffer_format_additional_constraints_t* additional) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_sysmem_get_description_from_collection(
    magma_sysmem_connection_t connection, magma_buffer_collection_t collection,
    magma_buffer_format_description_t* buffer_format_description_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_sysmem_get_buffer_handle_from_collection(magma_sysmem_connection_t connection,
                                                              magma_buffer_collection_t collection,
                                                              uint32_t index,
                                                              uint32_t* buffer_handle_out,
                                                              uint32_t* vmo_offset_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_count(magma_buffer_format_description_t description,
                                      uint32_t* count_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_is_secure(magma_buffer_format_description_t description,
                                          magma_bool_t* is_secure_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_cache_policy(magma_buffer_t buffer,
                                             magma_cache_policy_t* cache_policy_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_get_buffer_is_mappable(magma_buffer_t buffer, uint32_t flags,
                                            magma_bool_t* is_mappable_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_duplicate_handle(uint32_t buffer_handle, uint32_t* buffer_handle_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_release_buffer_handle(uint32_t buffer_handle) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_clean_cache(magma_buffer_t buffer, uint64_t offset, uint64_t size,
                                 magma_cache_operation_t operation) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_commit_buffer(magma_connection_t connection, magma_buffer_t buffer,
                                   uint64_t page_offset, uint64_t page_count) {
  return MAGMA_STATUS_OK;
}

magma_status_t magma_map_specific(magma_connection_t connection, magma_buffer_t buffer,
                                  uint64_t addr, uint64_t offset, uint64_t length) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_map_constrained(magma_connection_t connection, magma_buffer_t buffer,
                                     uint64_t length, uint64_t upper_limit, uint64_t alignment,
                                     void** addr_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_initialize_tracing(magma_handle_t channel) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_initialize_logging(magma_handle_t channel) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_poll(magma_poll_item_t* items, uint32_t count, uint64_t timeout_ns) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_connection_access_performance_counters(magma_connection_t connection,
                                                            magma_handle_t channel) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_connection_enable_performance_counters(magma_connection_t connection,
                                                            uint64_t* counters,
                                                            uint64_t counters_count) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_connection_create_performance_counter_buffer_pool(
    magma_connection_t connection, magma_perf_count_pool_t* pool_out,
    magma_handle_t* notification_handle_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_connection_release_performance_counter_buffer_pool(
    magma_connection_t connection, magma_perf_count_pool_t pool) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_connection_add_performance_counter_buffer_offsets_to_pool(
    magma_connection_t connection, magma_perf_count_pool_t pool, const magma_buffer_offset* offsets,
    uint64_t offset_count) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_connection_remove_performance_counter_buffer_from_pool(
    magma_connection_t connection, magma_perf_count_pool_t pool, magma_buffer_t buffer) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_connection_dump_performance_counters(magma_connection_t connection,
                                                          magma_perf_count_pool_t pool,
                                                          uint32_t trigger_id) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_connection_clear_performance_counters(magma_connection_t connection,
                                                           uint64_t* counters,
                                                           uint64_t counters_count) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_connection_read_performance_counter_completion(
    magma_connection_t connection, magma_perf_count_pool_t pool, uint32_t* trigger_id_out,
    uint64_t* buffer_id_out, uint32_t* buffer_offset_out, uint64_t* time_out,
    uint32_t* result_flags_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_buffer_set_name(magma_connection_t connection, magma_buffer_t buffer,
                                     const char* name) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}
