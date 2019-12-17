// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "platform_connection_client.h"

namespace magma {

class LinuxPlatformConnectionClient : public PlatformConnectionClient {
 public:
  magma_status_t ImportBuffer(PlatformBuffer* buffer) override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::ImportBuffer unimplemented");
  }

  magma_status_t ReleaseBuffer(uint64_t buffer_id) override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::ReleaseBuffer unimplemented");
  }

  magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type) override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::ImportObject unimplemented");
  }

  magma_status_t ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::ReleaseObject unimplemented");
  }

  void CreateContext(uint32_t* context_id_out) override {
    DMESSAGE("LinuxPlatformConnectionClient::CreateContext unimplemented");
  }

  void DestroyContext(uint32_t context_id) override {
    DMESSAGE("LinuxPlatformConnectionClient::DestroyContext unimplemented");
  }

  magma_status_t GetError() override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::GetError unimplemented");
  }

  magma_status_t MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                              uint64_t page_count, uint64_t flags) override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::MapBufferGpu unimplemented");
  }

  magma_status_t UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::UnmapBufferGpu unimplemented");
  }

  magma_status_t CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                              uint64_t page_count) override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::CommitBuffer unimplemented");
  }

  uint32_t GetNotificationChannelHandle() override { return 0; }

  magma_status_t ReadNotificationChannel(void* buffer, size_t buffer_size,
                                         size_t* buffer_size_out) override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::ReadNotificationChannel unimplemented");
  }

  magma_status_t WaitNotificationChannel(int64_t timeout_ns) override {
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED,
                    "LinuxPlatformConnectionClient::WaitNotificationChannel unimplemented");
  }

  void ExecuteCommandBufferWithResources(uint32_t context_id,
                                         magma_system_command_buffer* command_buffer,
                                         magma_system_exec_resource* resources,
                                         uint64_t* semaphores) override {
    DMESSAGE("LinuxPlatformConnectionClient::ExecuteCommandBufferWithResources unimplemented");
  }

  void ExecuteImmediateCommands(uint32_t context_id, uint64_t command_count,
                                magma_inline_command_buffer* command_buffers) override {
    DMESSAGE("LinuxPlatformConnectionClient::ExecuteImmediateCommands unimplemented");
  }
};

}  // namespace magma
