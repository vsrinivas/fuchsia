// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma/magma.h"

#include <assert.h>
#include <pthread.h>
#include <sys/mman.h>

#include "src/graphics/lib/magma/include/virtio/virtio_magma.h"
#include "src/graphics/lib/magma/src/libmagma_linux/virtmagma_util.h"

static pthread_once_t gOnceFlag = PTHREAD_ONCE_INIT;
static int gDefaultFd;

// Most magma interfaces get their file descriptor from a wrapped parameter (device, connection,
// etc) (or initially from the file descriptor "handle" in magma_device_import), but some interfaces
// don't have any such parameter; for those, we open the default device, and never close it.
static int get_default_fd() {
  pthread_once(&gOnceFlag, [] { gDefaultFd = open("/dev/magma0", O_RDWR); });
  assert(gDefaultFd >= 0);
  return gDefaultFd;
}

magma_status_t magma_poll(magma_poll_item_t* items, uint32_t count, uint64_t timeout_ns) {
#if VIRTMAGMA_DEBUG
  printf("%s\n", __PRETTY_FUNCTION__);
#endif

  if (count == 0)
    return MAGMA_STATUS_OK;

  magma_poll_item_t unwrapped_items[count];
  int32_t file_descriptor = -1;

  for (uint32_t i = 0; i < count; ++i) {
    unwrapped_items[i] = items[i];

    switch (items[i].type) {
      case MAGMA_POLL_TYPE_SEMAPHORE: {
        auto semaphore_wrapped = virtmagma_semaphore_t::Get(items[i].semaphore);
        unwrapped_items[i].semaphore = semaphore_wrapped->Object();

        if (i == 0) {
          auto semaphore0_parent_wrapped = virtmagma_connection_t::Get(semaphore_wrapped->Parent());
          file_descriptor = semaphore0_parent_wrapped->Parent().fd();
        }
        break;
      }

      case MAGMA_POLL_TYPE_HANDLE: {
        // Handles are not wrapped.
        break;
      }
    }
  }

  // Ensure host compatibility with 32bit guest
  static_assert(sizeof(magma_poll_item_t) % 8 == 0);

  virtio_magma_poll_ctrl_t request{};
  virtio_magma_poll_resp_t response{};
  request.hdr.type = VIRTIO_MAGMA_CMD_POLL;
  request.items = reinterpret_cast<uintptr_t>(&unwrapped_items[0]);
  // Send byte count so kernel knows how much memory to copy
  request.count = count * sizeof(magma_poll_item_t);
  request.timeout_ns = timeout_ns;

  if (file_descriptor == -1) {
    file_descriptor = get_default_fd();
  }

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

magma_status_t magma_execute_command(magma_connection_t connection, uint32_t context_id,
                                     struct magma_command_descriptor* descriptor) {
#if VIRTMAGMA_DEBUG
  printf("%s\n", __PRETTY_FUNCTION__);
#endif

  struct WireDescriptor {
    uint32_t resource_count;
    uint32_t command_buffer_count;
    uint32_t wait_semaphore_count;
    uint32_t signal_semaphore_count;
    uint64_t flags;
  };

  WireDescriptor wire_descriptor = {.resource_count = descriptor->resource_count,
                                    .command_buffer_count = descriptor->command_buffer_count,
                                    .wait_semaphore_count = descriptor->wait_semaphore_count,
                                    .signal_semaphore_count = descriptor->signal_semaphore_count,
                                    .flags = descriptor->flags};

  // Ensure host compatibility with 32bit guest
  static_assert(sizeof(magma_exec_command_buffer) % 8 == 0);
  static_assert(sizeof(magma_exec_resource) % 8 == 0);

  virtmagma_command_descriptor vdesc = {
      .descriptor_size = sizeof(wire_descriptor),
      .descriptor = reinterpret_cast<uintptr_t>(&wire_descriptor),
      .resource_size = sizeof(magma_exec_resource) * descriptor->resource_count,
      .resources = reinterpret_cast<uintptr_t>(descriptor->resources),
      .command_buffer_size = sizeof(magma_exec_command_buffer) * descriptor->command_buffer_count,
      .command_buffers = reinterpret_cast<uintptr_t>(descriptor->command_buffers),
      .semaphore_size = sizeof(uint64_t) *
                        (descriptor->wait_semaphore_count + descriptor->signal_semaphore_count),
      .semaphores = reinterpret_cast<uintptr_t>(descriptor->semaphore_ids)};

  virtio_magma_execute_command_ctrl request{.hdr = {.type = VIRTIO_MAGMA_CMD_EXECUTE_COMMAND}};
  virtio_magma_execute_command_resp response{};

  auto connection_wrapped = virtmagma_connection_t::Get(connection);
  request.connection = reinterpret_cast<uint64_t>(connection_wrapped->Object());
  request.context_id = context_id;
  request.descriptor = reinterpret_cast<uintptr_t>(&vdesc);

  int32_t file_descriptor = connection_wrapped->Parent().fd();

  if (!virtmagma_send_command(file_descriptor, &request, sizeof(request), &response,
                              sizeof(response))) {
    assert(false);
  }
  return static_cast<magma_status_t>(response.result_return);
}

magma_status_t magma_buffer_get_info(magma_connection_t connection, magma_buffer_t buffer,
                                     magma_buffer_info_t* info_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_initialize_tracing(magma_handle_t channel) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_virt_create_image(magma_connection_t connection,
                                       magma_image_create_info_t* create_info,
                                       magma_buffer_t* image_out) {
  auto connection_wrapped = virtmagma_connection_t::Get(connection);

#if VIRTMAGMA_DEBUG
  printf("%s\n", __PRETTY_FUNCTION__);
  printf("connection %lu\n", reinterpret_cast<uint64_t>(connection_wrapped->Object()));
  printf("create_info %p\n", create_info);
  printf("image_out %p\n", image_out);
#endif

  // Ensure host compatibility with 32bit guest
  static_assert(sizeof(magma_image_create_info_t) % 8 == 0);

  struct virtmagma_create_image_wrapper wrapper {
    .create_info = reinterpret_cast<uintptr_t>(create_info),
    .create_info_size = sizeof(magma_image_create_info_t),
  };

  virtio_magma_virt_create_image_ctrl_t request{
      .hdr = {.type = VIRTIO_MAGMA_CMD_VIRT_CREATE_IMAGE},
      .connection = reinterpret_cast<uint64_t>(connection_wrapped->Object()),
      .create_info = reinterpret_cast<uintptr_t>(&wrapper),
  };
  virtio_magma_virt_create_image_resp_t response{};

  if (!virtmagma_send_command(connection_wrapped->Parent().fd(), &request, sizeof(request),
                              &response, sizeof(response))) {
    assert(false);
    return DRET(MAGMA_STATUS_INTERNAL_ERROR);
  }
  if (response.hdr.type != VIRTIO_MAGMA_RESP_VIRT_CREATE_IMAGE)
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Wrong response header: %u", response.hdr.type);

  magma_status_t result_return = static_cast<decltype(result_return)>(response.result_return);
  if (result_return != MAGMA_STATUS_OK)
    return DRET(result_return);

  *image_out = virtmagma_buffer_t::Create(response.image_out, connection)->Wrap();

  return MAGMA_STATUS_OK;
}

magma_status_t magma_virt_get_image_info(magma_connection_t connection, magma_buffer_t image,
                                         magma_image_info_t* image_info_out) {
#if VIRTMAGMA_DEBUG
  printf("%s\n", __PRETTY_FUNCTION__);
  printf("image = %lu\n", image);
  printf("image_info_out = %p\n", image_info_out);
#endif

  auto connection_wrapped = virtmagma_connection_t::Get(connection);
  auto image_wrapped = virtmagma_buffer_t::Get(image);

  // Ensure host compatibility with 32bit guest
  static_assert(sizeof(magma_image_info_t) % 8 == 0);

  struct virtmagma_get_image_info_wrapper wrapper {
    .image_info_out = reinterpret_cast<uintptr_t>(image_info_out),
    .image_info_size = sizeof(magma_image_info_t),
  };

  virtio_magma_virt_get_image_info_ctrl_t request{
      .hdr =
          {
              .type = VIRTIO_MAGMA_CMD_VIRT_GET_IMAGE_INFO,
          },
      .connection = reinterpret_cast<uint64_t>(connection_wrapped->Object()),
      .image = image_wrapped->Object(),
      .image_info_out = reinterpret_cast<uintptr_t>(&wrapper),
  };
  virtio_magma_virt_get_image_info_resp_t response{};

  if (!virtmagma_send_command(connection_wrapped->Parent().fd(), &request, sizeof(request),
                              &response, sizeof(response))) {
    assert(false);
    return DRET(MAGMA_STATUS_INTERNAL_ERROR);
  }
  if (response.hdr.type != VIRTIO_MAGMA_RESP_VIRT_GET_IMAGE_INFO)
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Wrong response header: %u", response.hdr.type);

  magma_status_t result_return = static_cast<decltype(result_return)>(response.result_return);
  if (result_return != MAGMA_STATUS_OK)
    return DRET(result_return);

  return MAGMA_STATUS_OK;
}

magma_status_t magma_query(magma_device_t device, uint64_t id, magma_handle_t* result_buffer_out,
                           uint64_t* result_out) {
  auto device_wrapped = virtmagma_device_t::Get(device);
  device = device_wrapped->Object();

  int32_t file_descriptor = device_wrapped->Parent().fd();

  virtio_magma_query_ctrl_t request{
      .hdr = {.type = VIRTIO_MAGMA_CMD_QUERY}, .device = device, .id = id};
  virtio_magma_query_resp_t response{};

  bool success = virtmagma_send_command(file_descriptor, &request, sizeof(request), &response,
                                        sizeof(response));
  if (!success)
    return DRET(MAGMA_STATUS_INTERNAL_ERROR);

  magma_status_t status = static_cast<magma_status_t>(response.result_return);
  if (status != MAGMA_STATUS_OK)
    return DRET(status);

  if (response.hdr.type != VIRTIO_MAGMA_RESP_QUERY)
    return DRET(MAGMA_STATUS_INTERNAL_ERROR);

  int fd = static_cast<int>(response.result_buffer_out);
  if (fd < 0) {
    if (!result_out)
      return DRET(MAGMA_STATUS_INVALID_ARGS);

    *result_out = response.result_out;

    if (result_buffer_out)
      *result_buffer_out = -1;

    return MAGMA_STATUS_OK;
  }

  // If a buffer is present, it's an error to ignore it.
  if (!result_buffer_out) {
    close(fd);
    return DRET(MAGMA_STATUS_INVALID_ARGS);
  }

  *result_buffer_out = fd;
  return MAGMA_STATUS_OK;
}
