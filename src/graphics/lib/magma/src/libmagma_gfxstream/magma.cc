// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <virtgpu_drm.h>
#include <xf86drm.h>

#include <mutex>
#include <thread>

#include "AddressSpaceStream.h"
#include "EncoderDebug.h"
#include "gen/magma_enc.h"

// Rutabaga capsets - not needed for gfxstream backend?
#define VIRTIO_GPU_CAPSET_NONE 0
#define VIRTIO_GPU_CAPSET_VIRGL 1
#define VIRTIO_GPU_CAPSET_VIRGL2 2
#define VIRTIO_GPU_CAPSET_GFXSTREAM 3
#define VIRTIO_GPU_CAPSET_VENUS 4
#define VIRTIO_GPU_CAPSET_CROSS_DOMAIN 5

class IOStream;

static uint64_t get_ns_monotonic(bool raw) {
    struct timespec time;
    int ret = clock_gettime(raw ? CLOCK_MONOTONIC_RAW : CLOCK_MONOTONIC, &time);
    if (ret < 0) return 0;
    return static_cast<uint64_t>(time.tv_sec) * 1000000000ULL + time.tv_nsec;
}

class MagmaClientContext : public magma_encoder_context_t {
 public:
  MagmaClientContext(AddressSpaceStream* stream);

  AddressSpaceStream* stream() {
    return reinterpret_cast<AddressSpaceStream*>(magma_encoder_context_t::m_stream);
  }

  magma_status_t get_fd_for_buffer(magma_buffer_t buffer, int* fd_out);

  static magma_status_t magma_device_import(void* self, magma_handle_t device_channel,
                                            magma_device_t* device_out);
  static magma_status_t magma_query(void* self, magma_device_t device, uint64_t id,
                                    magma_handle_t* handle_out, uint64_t* value_out);
  static magma_status_t magma_get_buffer_handle2(void* self, magma_buffer_t buffer,
                                                 magma_handle_t* handle_out);
  static magma_status_t magma_poll(void* self, magma_poll_item_t* items, uint32_t count, uint64_t timeout_ns);

  magma_device_import_client_proc_t magma_device_import_enc_;
  magma_query_client_proc_t magma_query_enc_;
  magma_poll_client_proc_t magma_poll_enc_;
};

MagmaClientContext::MagmaClientContext(AddressSpaceStream* stream)
    : magma_encoder_context_t(stream, new ChecksumCalculator) {
  magma_device_import_enc_ = magma_client_context_t::magma_device_import;
  magma_query_enc_ = magma_client_context_t::magma_query;
  magma_poll_enc_ = magma_client_context_t::magma_poll;

  magma_client_context_t::magma_device_import = &MagmaClientContext::magma_device_import;
  magma_client_context_t::magma_query = &MagmaClientContext::magma_query;
  magma_client_context_t::magma_get_buffer_handle2 = &MagmaClientContext::magma_get_buffer_handle2;
  magma_client_context_t::magma_poll = &MagmaClientContext::magma_poll;
}

// static
magma_status_t MagmaClientContext::magma_device_import(void* self, magma_handle_t device_channel,
                                                       magma_device_t* device_out) {
  auto context = reinterpret_cast<MagmaClientContext*>(self);

  magma_handle_t placeholder = 0xacbd1234;  // not used

  magma_status_t status = context->magma_device_import_enc_(self, placeholder, device_out);

  // The local fd isn't needed, just close it.
  int fd = device_channel;
  close(fd);

  return status;
}

magma_status_t MagmaClientContext::get_fd_for_buffer(magma_buffer_t buffer, int* fd_out) {
  uint32_t gem_handle = 0;
  {
    uint64_t id = magma_get_buffer_id(this, buffer);
    if (id == 0) {
      ALOGE("%s: magma_get_buffer_id failed\n", __func__);
      return MAGMA_STATUS_INVALID_ARGS;
    }

    uint64_t size = magma_get_buffer_size(this, buffer);
    if (size == 0) {
      ALOGE("%s: magma_get_buffer_size failed\n", __func__);
      return MAGMA_STATUS_INVALID_ARGS;
    }

    struct drm_virtgpu_resource_create_blob drm_rc_blob = {
        .blob_mem = VIRTGPU_BLOB_MEM_HOST3D,
        .blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE | VIRTGPU_BLOB_FLAG_USE_SHAREABLE,
        .size = size,
        .blob_id = id,
    };

    int ret =
        drmIoctl(stream()->getRendernodeFd(), DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &drm_rc_blob);
    if (ret) {
      ALOGE("%s: DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB failed: %d (%s)\n", __func__, errno,
            strerror(errno));
      return MAGMA_STATUS_INTERNAL_ERROR;
    }

    gem_handle = drm_rc_blob.bo_handle;
  }

  int fd = -1;
  {
    struct drm_prime_handle args = {
        .handle = gem_handle,
        .flags = DRM_CLOEXEC | DRM_RDWR,
    };

    int ret = drmIoctl(stream()->getRendernodeFd(), DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
    if (ret) {
      ALOGE("%s: DRM_IOCTL_PRIME_HANDLE_TO_FD failed: %d (%s)\n", __func__, errno, strerror(errno));
    } else {
      fd = args.fd;
    }
  }

  {
    struct drm_gem_close close = {
        .handle = gem_handle,
    };

    int ret = drmIoctl(stream()->getRendernodeFd(), DRM_IOCTL_GEM_CLOSE, &close);
    if (ret) {
      ALOGE("%s: DRM_IOCTL_GEM_CLOSE failed: %d (%s)\n", __func__, errno, strerror(errno));
    }
  }

  if (fd < 0)
    return MAGMA_STATUS_INTERNAL_ERROR;

  *fd_out = fd;

  return MAGMA_STATUS_OK;
}

magma_status_t MagmaClientContext::magma_query(void* self, magma_device_t device, uint64_t id,
                                               magma_handle_t* handle_out, uint64_t* value_out) {
  auto context = reinterpret_cast<MagmaClientContext*>(self);

  magma_buffer_t buffer = 0;
  uint64_t value = 0;
  {
    magma_handle_t handle;
    magma_status_t status = context->magma_query_enc_(self, device, id, &handle, &value);
    if (status != MAGMA_STATUS_OK) {
      ALOGE("magma_query_enc failed: %d\n", status);
      return status;
    }
    // magma_buffer_t and magma_handle_t are both gem_handles on the server.
    buffer = handle;
  }

  if (!buffer) {
    if (!value_out)
      return MAGMA_STATUS_INVALID_ARGS;

    *value_out = value;

    if (handle_out) {
      *handle_out = -1;
    }

    return MAGMA_STATUS_OK;
  }

  if (!handle_out)
    return MAGMA_STATUS_INVALID_ARGS;

  int fd;
  magma_status_t status = context->get_fd_for_buffer(buffer, &fd);
  if (status != MAGMA_STATUS_OK)
    return status;

  *handle_out = fd;

  return MAGMA_STATUS_OK;
}

magma_status_t MagmaClientContext::magma_get_buffer_handle2(void* self, magma_buffer_t buffer,
                                                            magma_handle_t* handle_out) {
  auto context = reinterpret_cast<MagmaClientContext*>(self);

  int fd;
  magma_status_t status = context->get_fd_for_buffer(buffer, &fd);
  if (status != MAGMA_STATUS_OK)
    return status;

  *handle_out = fd;

  return MAGMA_STATUS_OK;
}

static int virtgpuOpen(uint32_t capset_id) {
  int fd = drmOpenRender(128);
  if (fd < 0) {
    ALOGE("Failed to open rendernode: %s", strerror(errno));
    return fd;
  }

  if (capset_id) {
    int ret;
    struct drm_virtgpu_context_init init = {0};
    struct drm_virtgpu_context_set_param ctx_set_params[2] = {{0}};

    ctx_set_params[0].param = VIRTGPU_CONTEXT_PARAM_NUM_RINGS;
    ctx_set_params[0].value = 1;
    init.num_params = 1;

    // TODO(b/218538495): A KI in the 5.4 kernel will sometimes result in capsets not
    // being properly queried.
#if defined(__linux__) && !defined(__ANDROID__)
    ctx_set_params[1].param = VIRTGPU_CONTEXT_PARAM_CAPSET_ID;
    ctx_set_params[1].value = capset_id;
    init.num_params++;
#endif

    init.ctx_set_params = (unsigned long long)&ctx_set_params[0];
    ret = drmIoctl(fd, DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &init);
    if (ret) {
      ALOGE("DRM_IOCTL_VIRTGPU_CONTEXT_INIT failed with %s, continuing without context...",
            strerror(errno));
    }
  }

  return fd;
}

// We can't pass a non-zero timeout to the server, as that would block the server from handling requests from
// other threads.  So we busy wait here, which isn't ideal; however if the server did block, gfxstream would
// busy wait for the response anyway.
magma_status_t MagmaClientContext::magma_poll(void* self, magma_poll_item_t* items, uint32_t count, uint64_t timeout_ns) {
   auto context = reinterpret_cast<MagmaClientContext*>(self);

    int64_t time_start = static_cast<int64_t>(get_ns_monotonic(false));

    int64_t abs_timeout_ns = time_start + timeout_ns;

    if (abs_timeout_ns < time_start) {
        abs_timeout_ns = std::numeric_limits<int64_t>::max();
    }

    bool warned_for_long_poll = false;

    while (true) {
       magma_status_t status = context->magma_poll_enc_(self, items, count, 0);

       if (status != MAGMA_STATUS_TIMED_OUT)
          return status;

       std::this_thread::yield();

       int64_t time_now = static_cast<int64_t>(get_ns_monotonic(false));

       // TODO(fxb/TBD) - the busy loop should probably backoff after some time
       if (!warned_for_long_poll && time_now - time_start > 5000000000) {
          ALOGE("magma_poll: long poll detected (%lu us)", (time_now - time_start) / 1000);
          warned_for_long_poll = true;
       }

       if (time_now >= abs_timeout_ns)
          break;
    }

    return MAGMA_STATUS_TIMED_OUT;
}

magma_client_context_t* GetMagmaContext() {
  static MagmaClientContext* s_context;
  static std::once_flag once_flag;

  std::call_once(once_flag, []() {
    struct StreamCreate streamCreate = {0};
    streamCreate.streamHandle = virtgpuOpen(VIRTIO_GPU_CAPSET_GFXSTREAM);
    if (streamCreate.streamHandle < 0) {
      ALOGE("Failed to open virtgpu for ASG host connection\n");
      return;
    }

    auto stream = createVirtioGpuAddressSpaceStream(streamCreate);
    assert(stream);

    // RenderThread expects flags: send zero 'clientFlags' to the host.
    {
      auto pClientFlags =
          reinterpret_cast<unsigned int*>(stream->allocBuffer(sizeof(unsigned int)));
      *pClientFlags = 0;
      stream->commitBuffer(sizeof(unsigned int));
    }

    s_context = new MagmaClientContext(stream);

    printf("Created new context\n");
    fflush(stdout);
  });

  return s_context;
}

// Used in magma_entry.cpp
#define GET_CONTEXT magma_client_context_t* ctx = GetMagmaContext()

#include "gen/magma_entry.cpp"

void encoderLog(const char* format, ...) {
  va_list list;
  va_start(list, format);
  vprintf(format, list);
  printf("\n");
  fflush(stdout);
  va_end(list);
}

// Stubs
extern "C" {

magma_status_t magma_execute_command(magma_connection_t connection, uint32_t context_id,
                                     struct magma_command_descriptor* descriptor) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_execute_immediate_commands2(
    magma_connection_t connection, uint32_t context_id, uint64_t command_count,
    struct magma_inline_command_buffer* command_buffers) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_export(magma_connection_t connection, magma_buffer_t buffer,
                            magma_handle_t* buffer_handle_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_import(magma_connection_t connection, magma_handle_t buffer_handle,
                            magma_buffer_t* buffer_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_export_semaphore(magma_connection_t connection, magma_semaphore_t semaphore,
                                      magma_handle_t* semaphore_handle_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_import_semaphore(magma_connection_t connection,
                                      magma_handle_t semaphore_handle,
                                      magma_semaphore_t* semaphore_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_handle_t magma_get_notification_channel_handle(magma_connection_t connection) { return 0; }

magma_status_t magma_virt_create_image(magma_connection_t connection,
                                       magma_image_create_info_t* create_info,
                                       magma_buffer_t* image_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_virt_get_image_info(magma_connection_t connection, magma_buffer_t image,
                                         magma_image_info_t* image_info_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_buffer_range_op(magma_connection_t connection, magma_buffer_t buffer,
                                     uint32_t options, uint64_t start_offset, uint64_t length) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_read_notification_channel2(magma_connection_t connection, void* buffer,
                                                uint64_t buffer_size, uint64_t* buffer_size_out,
                                                magma_bool_t* more_data_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t magma_flush(magma_connection_t connection) { return MAGMA_STATUS_UNIMPLEMENTED; }
}
