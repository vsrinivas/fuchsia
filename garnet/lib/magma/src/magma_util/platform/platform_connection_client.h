// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_CLIENT_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_CLIENT_H_

#include <memory>

#include "magma.h"
#include "magma_util/macros.h"
#include "magma_util/status.h"
#include "msd_defs.h"
#include "platform_buffer.h"
#include "platform_object.h"
#include "platform_thread.h"

namespace magma {

// Any implementation of PlatformConnectionClient shall be threadsafe.
class PlatformConnectionClient : public magma_connection {
 public:
  virtual ~PlatformConnectionClient() {}

  static std::unique_ptr<PlatformConnectionClient> Create(uint32_t device_handle,
                                                          uint32_t device_notification_handle);

  // Imports a buffer for use in the system driver
  virtual magma_status_t ImportBuffer(PlatformBuffer* buffer) = 0;
  // Destroys the buffer with |buffer_id| within this connection
  // returns false if |buffer_id| has not been imported
  virtual magma_status_t ReleaseBuffer(uint64_t buffer_id) = 0;

  // Imports an object for use in the system driver
  virtual magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type) = 0;

  // Releases the connection's reference to the given object.
  virtual magma_status_t ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) = 0;

  // Creates a context and returns the context id
  virtual void CreateContext(uint32_t* context_id_out) = 0;
  // Destroys a context for the given id
  virtual void DestroyContext(uint32_t context_id) = 0;

  virtual magma_status_t GetError() = 0;

  virtual magma_status_t MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                                      uint64_t page_count, uint64_t flags) = 0;

  virtual magma_status_t UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) = 0;

  virtual magma_status_t CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                      uint64_t page_count) = 0;

  virtual uint32_t GetNotificationChannelHandle() = 0;
  virtual magma_status_t ReadNotificationChannel(void* buffer, size_t buffer_size,
                                                 size_t* buffer_size_out) = 0;
  virtual magma_status_t WaitNotificationChannel(int64_t timeout_ns) = 0;
  virtual void ExecuteCommandBufferWithResources(uint32_t context_id,
                                                 magma_system_command_buffer* command_buffer,
                                                 magma_system_exec_resource* resources,
                                                 uint64_t* semaphores) = 0;
  virtual void ExecuteImmediateCommands(uint32_t context_id, uint64_t command_count,
                                        magma_inline_command_buffer* command_buffers) = 0;

  static PlatformConnectionClient* cast(magma_connection_t connection) {
    DASSERT(connection);
    DASSERT(connection->magic_ == kMagic);
    return static_cast<PlatformConnectionClient*>(connection);
  }

 protected:
  PlatformConnectionClient() { magic_ = kMagic; }

 private:
  static const uint32_t kMagic = 0x636f6e6e;  // "conn" (Connection)
};

}  // namespace magma

#endif  // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_CLIENT_H_
