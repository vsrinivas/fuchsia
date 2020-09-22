// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"

#include <sys/mman.h>

#include <vector>

#include "src/graphics/lib/magma/include/virtio/virtio_magma.h"
#include "src/graphics/lib/magma/src/libmagma_linux/virtmagma_util.h"

// TODO(fxbug.dev/13228): support an object that is a parent of magma_connection_t
// This class is a temporary workaround to support magma APIs that do not
// pass in generic objects capable of holding file descriptors, e.g.
// magma_duplicate_handle.
std::map<uint32_t, virtmagma_handle_t*>& GlobalHandleTable() {
  static std::map<uint32_t, virtmagma_handle_t*> ht;
  return ht;
}

magma_status_t magma_map(magma_connection_t connection, magma_buffer_t buffer, void** addr_out) {
  *addr_out = nullptr;

  auto connection_wrapped = virtmagma_connection_t::Get(connection);
  connection = connection_wrapped->Object();
  int32_t file_descriptor = connection_wrapped->Parent().first;
  int32_t file_descriptor_mmap = connection_wrapped->Parent().second;
  auto buffer_wrapped = virtmagma_buffer_t::Get(buffer);
  buffer = buffer_wrapped->Object();

  virtio_magma_map_ctrl_t request{};
  struct {
    virtio_magma_map_resp_t virtio_response;
    size_t size_to_mmap_out;
  } response{};
  request.hdr.type = VIRTIO_MAGMA_CMD_MAP;
  request.connection = reinterpret_cast<decltype(request.connection)>(connection);
  request.buffer = reinterpret_cast<decltype(request.buffer)>(buffer);

  if (!virtmagma_send_command(file_descriptor, &request, sizeof(request), &response,
                              sizeof(response))) {
    return MAGMA_STATUS_INTERNAL_ERROR;
  }
  if (response.virtio_response.hdr.type != VIRTIO_MAGMA_RESP_MAP) {
    return MAGMA_STATUS_INTERNAL_ERROR;
  }

  // The only simple way to construct a new vm_area_struct in the kernel is to reuse
  // the mmap call path.
  void* mapped_addr = mmap(nullptr, response.size_to_mmap_out, PROT_READ | PROT_WRITE, MAP_SHARED,
                           file_descriptor_mmap, 0);
  if (mapped_addr == MAP_FAILED) {
    return MAGMA_STATUS_INTERNAL_ERROR;
  }
  *addr_out = mapped_addr;

  magma_status_t result_return =
      static_cast<decltype(result_return)>(response.virtio_response.result_return);
  return result_return;
}

magma_status_t magma_map_aligned(magma_connection_t connection, magma_buffer_t buffer,
                                 uint64_t alignment, void** addr_out) {
  *addr_out = nullptr;
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_map_specific(magma_connection_t connection, magma_buffer_t buffer,
                                  uint64_t addr, uint64_t offset, uint64_t length) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_wait_semaphores(const magma_semaphore_t* semaphores, uint32_t count,
                                     uint64_t timeout_ms, magma_bool_t wait_all) {
  if (count == 0)
    return MAGMA_STATUS_OK;

  auto semaphore0_wrapped = virtmagma_semaphore_t::Get(semaphores[0]);
  auto semaphore0_parent_wrapped = virtmagma_connection_t::Get(semaphore0_wrapped->Parent());
  int32_t file_descriptor = semaphore0_parent_wrapped->Parent().first;

  virtio_magma_wait_semaphores_ctrl_t request{};
  virtio_magma_wait_semaphores_resp_t response{};
  request.hdr.type = VIRTIO_MAGMA_CMD_WAIT_SEMAPHORES;
  std::vector<magma_semaphore_t> unwrapped_semaphores(count);
  for (uint32_t i = 0; i < count; ++i) {
    unwrapped_semaphores[i] = virtmagma_semaphore_t::Get(semaphores[i])->Object();
  }
  request.semaphores = reinterpret_cast<decltype(request.semaphores)>(unwrapped_semaphores.data());
  request.count = count;
  request.timeout_ms = timeout_ms;
  request.wait_all = wait_all;

  if (!virtmagma_send_command(file_descriptor, &request, sizeof(request), &response,
                              sizeof(response)))
    return MAGMA_STATUS_INTERNAL_ERROR;
  if (response.hdr.type != VIRTIO_MAGMA_RESP_WAIT_SEMAPHORES)
    return MAGMA_STATUS_INTERNAL_ERROR;

  magma_status_t result_return = static_cast<decltype(result_return)>(response.result_return);
  return result_return;
}

magma_status_t magma_poll(magma_poll_item_t* items, uint32_t count, uint64_t timeout_ns) {
  if (count == 0)
    return MAGMA_STATUS_OK;

  std::vector<magma_poll_item_t> unwrapped_items(count);
  int32_t file_descriptor = -1;

  for (uint32_t i = 0; i < count; ++i) {
    unwrapped_items[i] = items[i];

    switch (items[i].type) {
      case MAGMA_POLL_TYPE_SEMAPHORE: {
        auto semaphore_wrapped = virtmagma_semaphore_t::Get(items[i].semaphore);
        unwrapped_items[i].semaphore = semaphore_wrapped->Object();

        if (i == 0) {
          auto semaphore0_parent_wrapped = virtmagma_connection_t::Get(semaphore_wrapped->Parent());
          file_descriptor = semaphore0_parent_wrapped->Parent().first;
        }
        break;
      }

      case MAGMA_POLL_TYPE_HANDLE: {
        auto iter = GlobalHandleTable().find(items[i].handle);
        if (iter == GlobalHandleTable().end()) {
          return MAGMA_STATUS_INVALID_ARGS;  // Not found
        }

        virtmagma_handle_t* handle = iter->second;
        unwrapped_items[i].handle = handle->Object();

        if (i == 0) {
          file_descriptor = handle->Parent();
        }
        break;
      }
    }
  }

  virtio_magma_poll_ctrl_t request{};
  virtio_magma_poll_resp_t response{};
  request.hdr.type = VIRTIO_MAGMA_CMD_POLL;
  request.items = reinterpret_cast<uint64_t>(unwrapped_items.data());
  // Send byte count so kernel knows how much memory to copy
  request.count = count * sizeof(magma_poll_item_t);
  request.timeout_ns = timeout_ns;

  if (!virtmagma_send_command(file_descriptor, &request, sizeof(request), &response,
                              sizeof(response)))
    return MAGMA_STATUS_INTERNAL_ERROR;
  if (response.hdr.type != VIRTIO_MAGMA_RESP_POLL)
    return MAGMA_STATUS_INTERNAL_ERROR;

  magma_status_t result_return = static_cast<decltype(result_return)>(response.result_return);
  if (result_return != MAGMA_STATUS_OK)
    return result_return;

  // Update the results
  for (uint32_t i = 0; i < count; i++) {
    items[i].result = unwrapped_items[i].result;
  }

  return MAGMA_STATUS_OK;
}

void magma_execute_command_buffer_with_resources(magma_connection_t connection, uint32_t context_id,
                                                 struct magma_system_command_buffer* command_buffer,
                                                 struct magma_system_exec_resource* resources,
                                                 uint64_t* semaphore_ids) {
  virtmagma_command_buffer virt_command_buffer;
  virt_command_buffer.command_buffer_size = sizeof(magma_system_command_buffer);
  virt_command_buffer.command_buffer =
      reinterpret_cast<decltype(virt_command_buffer.command_buffer)>(command_buffer);
  virt_command_buffer.resource_size =
      sizeof(magma_system_exec_resource) * command_buffer->resource_count;
  virt_command_buffer.resources =
      reinterpret_cast<decltype(virt_command_buffer.resources)>(resources);
  virt_command_buffer.semaphore_size = sizeof(uint64_t) * (command_buffer->wait_semaphore_count +
                                                           command_buffer->signal_semaphore_count);
  virt_command_buffer.semaphores =
      reinterpret_cast<decltype(virt_command_buffer.resources)>(semaphore_ids);

  virtio_magma_execute_command_buffer_with_resources_ctrl request{};
  virtio_magma_execute_command_buffer_with_resources_resp response{};
  request.hdr.type = VIRTIO_MAGMA_CMD_EXECUTE_COMMAND_BUFFER_WITH_RESOURCES;

  auto connection_wrapped = virtmagma_connection_t::Get(connection);
  request.connection = reinterpret_cast<decltype(request.connection)>(connection_wrapped->Object());

  request.context_id = context_id;
  request.command_buffer = reinterpret_cast<decltype(request.command_buffer)>(&virt_command_buffer);

  int32_t file_descriptor = connection_wrapped->Parent().first;

  if (!virtmagma_send_command(file_descriptor, &request, sizeof(request), &response,
                              sizeof(response))) {
    assert(false);
  }
}

magma_status_t magma_initialize_tracing(magma_handle_t channel) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}
