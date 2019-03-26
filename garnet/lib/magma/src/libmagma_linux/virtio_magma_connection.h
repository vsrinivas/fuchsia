// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_LIBMAGMA_LINUX_VIRTIO_MAGMA_CONNECTION_H_
#define GARNET_LIB_MAGMA_SRC_LIBMAGMA_LINUX_VIRTIO_MAGMA_CONNECTION_H_

#include "magma.h"
#include <memory>

namespace magma {

struct VirtioMagmaConnection : public magma_connection {
public:
    VirtioMagmaConnection(int32_t virtio_fd, int32_t connection_fd)
        : magma_connection{kMagic}, virtio_fd_(virtio_fd), connection_fd_(connection_fd)
    {
    }

    static bool WriteDriverToFilesystem(int32_t virtio_fd);
    static magma_status_t Query(int32_t virtio_fd, uint64_t id, uint64_t* value_out);
    static std::unique_ptr<VirtioMagmaConnection> Create(int32_t virtio_fd);
    void Release();
    magma_status_t GetError();
    void CreateContext(uint32_t* context_id_out);
    void ReleaseContext(uint32_t context_id);
    magma_status_t CreateBuffer(uint64_t size, uint64_t* size_out, magma_buffer_t* buffer_out);
    void ReleaseBuffer(magma_buffer_t buffer);
    uint64_t GetBufferId(magma_buffer_t buffer);
    uint64_t GetBufferSize(magma_buffer_t buffer);
    magma_status_t CleanCache(magma_buffer_t buffer, uint64_t offset, uint64_t size,
                              magma_cache_operation_t operation);
    magma_status_t SetCachePolicy(magma_buffer_t buffer, magma_cache_policy_t policy);
    magma_status_t Map(magma_buffer_t buffer, void** addr_out);
    magma_status_t MapAligned(magma_buffer_t buffer, uint64_t alignment, void** addr_out);
    magma_status_t MapSpecific(magma_buffer_t buffer, uint64_t addr);
    magma_status_t Unmap(magma_buffer_t buffer);
    void MapBufferGpu(magma_buffer_t buffer, uint64_t page_offset, uint64_t page_count,
                      uint64_t gpu_va, uint64_t map_flags);
    void UnmapBufferGpu(magma_buffer_t buffer, uint64_t gpu_va);
    void CommitBuffer(magma_buffer_t buffer, uint64_t page_offset, uint64_t page_count);
    magma_status_t Export(magma_buffer_t buffer, uint32_t* buffer_handle_out);
    magma_status_t Import(uint32_t buffer_handle, magma_buffer_t* buffer_out);
    magma_status_t CreateCommandBuffer(uint64_t size, magma_buffer_t* buffer_out);
    void ReleaseCommandBuffer(magma_buffer_t command_buffer);
    void SubmitCommandBuffer(magma_buffer_t command_buffer, uint32_t context_id);
    void ExecuteImmediateCommands(uint32_t context_id, uint64_t command_count,
                                  struct magma_system_inline_command_buffer* command_buffers);
    magma_status_t CreateSemaphore(magma_semaphore_t* semaphore_out);
    void ReleaseSemaphore(magma_semaphore_t semaphore);
    uint64_t GetSemaphoreId(magma_semaphore_t semaphore);
    void SignalSemaphore(magma_semaphore_t semaphore);
    void ResetSemaphore(magma_semaphore_t semaphore);
    magma_status_t WaitSemaphores(const magma_semaphore_t* semaphores, uint32_t count,
                                  uint64_t timeout_ms, magma_bool_t wait_all);
    magma_status_t ExportSemaphore(magma_semaphore_t semaphore, uint32_t* semaphore_handle_out);
    magma_status_t ImportSemaphore(uint32_t semaphore_handle, magma_semaphore_t* semaphore_out);
    int32_t GetNotificationChannelFD();
    magma_status_t ReadNotificationChannel(void* buffer, uint64_t buffer_size,
                                           uint64_t* buffer_size_out);

    static VirtioMagmaConnection* cast(magma_connection_t connection);

private:
    VirtioMagmaConnection(const VirtioMagmaConnection&) = delete;
    VirtioMagmaConnection(VirtioMagmaConnection&&) = default;
    // Call the handshake ioctl. Returns the virtio-magma interface version implemented
    // by the virtio-magma driver, or 0 on handshake failure.
    static uint32_t Handshake(int32_t file_descriptor);

    int32_t virtio_fd_ = -1;
    int32_t connection_fd_ = -1;
    static const uint32_t kMagic = 0x76697274; // "virt" (Virtual Connection)
};

} // namespace magma

#endif // GARNET_LIB_MAGMA_SRC_LIBMAGMA_LINUX_VIRTIO_MAGMA_CONNECTION_H_
