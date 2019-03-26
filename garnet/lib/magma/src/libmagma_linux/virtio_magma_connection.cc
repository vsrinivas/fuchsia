// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtio_magma_connection.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "virtmagma.h"
#include <cstring> // memcpy
#include <errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>

namespace magma {

bool VirtioMagmaConnection::WriteDriverToFilesystem(int32_t virtio_fd)
{
    if (!Handshake(virtio_fd)) {
        DRETF(false, "virtio_fd does not implement VirtioMagma");
    }

    virtmagma_ioctl_args_get_driver args{};
    if (ioctl(virtio_fd, VIRTMAGMA_IOCTL_GET_DRIVER, &args)) {
        DRETF(false, "ioctl(GET_DRIVER) failed: %d", errno);
    }

    return true;
}

magma_status_t VirtioMagmaConnection::Query(int32_t virtio_fd, uint64_t id, uint64_t* value_out)
{
    *value_out = 0;

    if (!Handshake(virtio_fd)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "virtio_fd does not implement VirtioMagma");
    }

    virtmagma_ioctl_args_query args{};
    args.id = id;
    if (ioctl(virtio_fd, VIRTMAGMA_IOCTL_QUERY, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(QUERY) failed: %d", errno);
    }

    *value_out = args.value_out;
    return args.status_return;
}

std::unique_ptr<VirtioMagmaConnection> VirtioMagmaConnection::Create(int32_t virtio_fd)
{
    uint32_t version_out = Handshake(virtio_fd);
    if (!version_out) {
        return DRETP(nullptr, "virtio_fd does not implement VirtioMagma");
    }

    uint32_t version_major = 0;
    uint32_t version_minor = 0;
    uint32_t version_patch = 0;
    VIRTMAGMA_GET_VERSION(version_out, version_major, version_minor, version_patch);
    DLOG("Connected to VirtioMagma driver version %u.%u.%u", version_major, version_minor,
         version_patch);

    virtmagma_ioctl_args_create_connection args{};
    if (ioctl(virtio_fd, VIRTMAGMA_IOCTL_CREATE_CONNECTION, &args)) {
        return DRETP(nullptr, "ioctl(CREATE_CONNECTION) failed: %d", errno);
    }

    return std::make_unique<VirtioMagmaConnection>(virtio_fd, args.connection_return);
}

void VirtioMagmaConnection::Release()
{
    virtmagma_ioctl_args_release_connection args{};
    args.connection = connection_fd_;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_RELEASE_CONNECTION, &args)) {
        DLOG("ioctl(RELEASE_CONNECTION) failed: %d", errno);
        return;
    }
}

magma_status_t VirtioMagmaConnection::GetError()
{
    virtmagma_ioctl_args_get_error args{};
    args.connection = connection_fd_;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_GET_ERROR, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(GET_ERROR) failed: %d", errno);
    }

    return args.status_return;
}

void VirtioMagmaConnection::CreateContext(uint32_t* context_id_out)
{
    *context_id_out = 0;

    virtmagma_ioctl_args_create_context args{};
    args.connection = connection_fd_;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_CREATE_CONTEXT, &args)) {
        DLOG("ioctl(CREATE_CONTEXT) failed: %d", errno);
        return;
    }

    *context_id_out = args.context_id_out;
}

void VirtioMagmaConnection::ReleaseContext(uint32_t context_id)
{
    virtmagma_ioctl_args_release_context args{};
    args.connection = connection_fd_;
    args.context_id = context_id;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_RELEASE_CONTEXT, &args)) {
        DLOG("ioctl(RELEASE_CONTEXT) failed: %d", errno);
    }
}

magma_status_t VirtioMagmaConnection::CreateBuffer(uint64_t size, uint64_t* size_out,
                                                   magma_buffer_t* buffer_out)
{
    *size_out = 0;
    *buffer_out = 0;

    virtmagma_ioctl_args_create_buffer args{};
    args.connection = connection_fd_;
    args.size = size;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_CREATE_BUFFER, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(CREATE_BUFFER) failed: %d", errno);
    }

    *size_out = args.size_out;
    *buffer_out = args.buffer_out;
    return args.status_return;
}

void VirtioMagmaConnection::ReleaseBuffer(magma_buffer_t buffer)
{
    virtmagma_ioctl_args_release_buffer args{};
    args.connection = connection_fd_;
    args.buffer = buffer;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_RELEASE_BUFFER, &args)) {
        DLOG("ioctl(RELEASE_BUFFER) failed: %d", errno);
    }
}

uint64_t VirtioMagmaConnection::GetBufferId(magma_buffer_t buffer)
{
    virtmagma_ioctl_args_get_buffer_id args{};
    args.buffer = buffer;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_GET_BUFFER_ID, &args)) {
        DMESSAGE("ioctl(GET_BUFFER_ID) failed: %d", errno);
        return MAGMA_INVALID_OBJECT_ID;
    }

    return args.id_return;
}

uint64_t VirtioMagmaConnection::GetBufferSize(magma_buffer_t buffer)
{
    virtmagma_ioctl_args_get_buffer_size args{};
    args.buffer = buffer;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_GET_BUFFER_SIZE, &args)) {
        DMESSAGE("ioctl(GET_BUFFER_SIZE) failed: %d", errno);
        return MAGMA_INVALID_OBJECT_ID;
    }

    return args.size_return;
}

magma_status_t VirtioMagmaConnection::CleanCache(magma_buffer_t buffer, uint64_t offset,
                                                 uint64_t size, magma_cache_operation_t operation)
{
    virtmagma_ioctl_args_clean_cache args{};
    args.buffer = buffer;
    args.offset = offset;
    args.size = size;
    args.operation = operation;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_CLEAN_CACHE, &args)) {
        DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(CLEAN_CACHE) failed: %d", errno);
    }

    return args.status_return;
}

magma_status_t VirtioMagmaConnection::SetCachePolicy(magma_buffer_t buffer,
                                                     magma_cache_policy_t policy)
{
    virtmagma_ioctl_args_set_cache_policy args{};
    args.buffer = buffer;
    args.policy = policy;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_SET_CACHE_POLICY, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(SET_CACHE_POLICY) failed: %d", errno);
    }

    return args.status_return;
}

magma_status_t VirtioMagmaConnection::Map(magma_buffer_t buffer, void** addr_out)
{
    *addr_out = nullptr;

    virtmagma_ioctl_args_map args{};
    args.connection = connection_fd_;
    args.buffer = buffer;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_MAP, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(MAP) failed: %d", errno);
    }

    *addr_out = reinterpret_cast<void*>(args.addr_out);
    return args.status_return;
}

magma_status_t VirtioMagmaConnection::MapAligned(magma_buffer_t buffer, uint64_t alignment,
                                                 void** addr_out)
{
    *addr_out = nullptr;

    virtmagma_ioctl_args_map_aligned args{};
    args.connection = connection_fd_;
    args.buffer = buffer;
    args.alignment = alignment;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_MAP_ALIGNED, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(MAP_ALIGNED) failed: %d", errno);
    }

    *addr_out = reinterpret_cast<void*>(args.addr_out);
    return args.status_return;
}

magma_status_t VirtioMagmaConnection::MapSpecific(magma_buffer_t buffer, uint64_t addr)
{
    virtmagma_ioctl_args_map_specific args{};
    args.connection = connection_fd_;
    args.buffer = buffer;
    args.addr = addr;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_MAP_SPECIFIC, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(MAP_SPECIFIC) failed: %d", errno);
    }

    return args.status_return;
}

magma_status_t VirtioMagmaConnection::Unmap(magma_buffer_t buffer)
{
    virtmagma_ioctl_args_unmap args{};
    args.connection = connection_fd_;
    args.buffer = buffer;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_UNMAP, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(UNMAP) failed: %d", errno);
    }

    return args.status_return;
}

void VirtioMagmaConnection::MapBufferGpu(magma_buffer_t buffer, uint64_t page_offset,
                                         uint64_t page_count, uint64_t gpu_va, uint64_t map_flags)
{
    virtmagma_ioctl_args_map_buffer_gpu args{};
    args.connection = connection_fd_;
    args.buffer = buffer;
    args.page_offset = page_offset;
    args.page_count = page_count;
    args.gpu_va = gpu_va;
    args.map_flags = map_flags;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_MAP_BUFFER_GPU, &args)) {
        DLOG("ioctl(MAP_BUFFER_GPU) failed: %d", errno);
    }
}

void VirtioMagmaConnection::UnmapBufferGpu(magma_buffer_t buffer, uint64_t gpu_va)
{
    virtmagma_ioctl_args_unmap_buffer_gpu args{};
    args.connection = connection_fd_;
    args.buffer = buffer;
    args.gpu_va = gpu_va;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_UNMAP_BUFFER_GPU, &args)) {
        DLOG("ioctl(UNMAP_BUFFER_GPU) failed: %d", errno);
    }
}

void VirtioMagmaConnection::CommitBuffer(magma_buffer_t buffer, uint64_t page_offset,
                                         uint64_t page_count)
{
    virtmagma_ioctl_args_commit_buffer args{};
    args.connection = connection_fd_;
    args.buffer = buffer;
    args.page_offset = page_offset;
    args.page_count = page_count;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_COMMIT_BUFFER, &args)) {
        DLOG("ioctl(COMMIT_BUFFER) failed: %d", errno);
    }
}

magma_status_t VirtioMagmaConnection::Export(magma_buffer_t buffer, uint32_t* buffer_handle_out)
{
    *buffer_handle_out = 0;

    virtmagma_ioctl_args_export args{};
    args.connection = connection_fd_;
    args.buffer = buffer;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_EXPORT, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(EXPORT) failed: %d", errno);
    }

    *buffer_handle_out = args.buffer_handle_out;
    return args.status_return;
}

magma_status_t VirtioMagmaConnection::Import(uint32_t buffer_handle, magma_buffer_t* buffer_out)
{
    *buffer_out = 0;

    virtmagma_ioctl_args_import args{};
    args.connection = connection_fd_;
    args.buffer_handle = buffer_handle;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_IMPORT, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(IMPORT) failed: %d", errno);
    }

    *buffer_out = args.buffer_out;
    return args.status_return;
}

magma_status_t VirtioMagmaConnection::CreateCommandBuffer(uint64_t size, magma_buffer_t* buffer_out)
{
    *buffer_out = 0;

    virtmagma_ioctl_args_create_command_buffer args{};
    args.connection = connection_fd_;
    args.size = size;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_CREATE_COMMAND_BUFFER, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(CREATE_COMMAND_BUFFER) failed: %d",
                        errno);
    }

    *buffer_out = args.buffer_out;
    return args.status_return;
}

void VirtioMagmaConnection::ReleaseCommandBuffer(magma_buffer_t command_buffer)
{
    virtmagma_ioctl_args_release_command_buffer args{};
    args.connection = connection_fd_;
    args.command_buffer = command_buffer;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_RELEASE_COMMAND_BUFFER, &args)) {
        DLOG("ioctl(RELEASE_COMMAND_BUFFER) failed: %d", errno);
    }
}

void VirtioMagmaConnection::SubmitCommandBuffer(magma_buffer_t command_buffer, uint32_t context_id)
{
    virtmagma_ioctl_args_submit_command_buffer args{};
    args.connection = connection_fd_;
    args.command_buffer = command_buffer;
    args.context_id = context_id;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_SUBMIT_COMMAND_BUFFER, &args)) {
        DLOG("ioctl(SUBMIT_COMMAND_BUFFER) failed: %d", errno);
    }
}

void VirtioMagmaConnection::ExecuteImmediateCommands(
    uint32_t context_id, uint64_t command_count,
    struct magma_system_inline_command_buffer* command_buffers)
{
    virtmagma_ioctl_args_execute_immediate_commands args{};
    args.connection = connection_fd_;
    args.context_id = context_id;
    args.command_count = command_count;
    args.commands_addr = reinterpret_cast<uint64_t>(command_buffers);
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_EXECUTE_IMMEDIATE_COMMANDS, args)) {
        DLOG("ioctl(SUBMIT_COMMAND_BUFFER) failed: %d", errno);
    }
}

magma_status_t VirtioMagmaConnection::CreateSemaphore(magma_semaphore_t* semaphore_out)
{
    *semaphore_out = 0;

    virtmagma_ioctl_args_create_semaphore args{};
    args.connection = connection_fd_;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_CREATE_SEMAPHORE, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(CREATE_SEMAPHORE) failed: %d", errno);
    }

    *semaphore_out = args.semaphore_out;
    return args.status_return;
}

void VirtioMagmaConnection::ReleaseSemaphore(magma_semaphore_t semaphore)
{
    virtmagma_ioctl_args_release_semaphore args{};
    args.connection = connection_fd_;
    args.semaphore = semaphore;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_RELEASE_SEMAPHORE, &args)) {
        DLOG("ioctl(RELEASE_SEMAPHORE) failed: %d", errno);
    }
}

uint64_t VirtioMagmaConnection::GetSemaphoreId(magma_semaphore_t semaphore)
{
    virtmagma_ioctl_args_get_semaphore_id args{};
    args.semaphore = semaphore;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_GET_SEMAPHORE_ID, &args)) {
        DMESSAGE("ioctl(GET_SEMAPHORE_ID) failed: %d", errno);
        return MAGMA_INVALID_OBJECT_ID;
    }

    return args.id_return;
}

void VirtioMagmaConnection::SignalSemaphore(magma_semaphore_t semaphore)
{
    virtmagma_ioctl_args_signal_semaphore args{};
    args.semaphore = semaphore;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_SIGNAL_SEMAPHORE, &args)) {
        DLOG("ioctl(SIGNAL_SEMAPHORE) failed: %d", errno);
    }
}

void VirtioMagmaConnection::ResetSemaphore(magma_semaphore_t semaphore)
{
    virtmagma_ioctl_args_reset_semaphore args{};
    args.semaphore = semaphore;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_RESET_SEMAPHORE, &args)) {
        DLOG("ioctl(RESET_SEMAPHORE) failed: %d", errno);
    }
}

magma_status_t VirtioMagmaConnection::WaitSemaphores(const magma_semaphore_t* semaphores,
                                                     uint32_t count, uint64_t timeout_ms,
                                                     magma_bool_t wait_all)
{
    virtmagma_ioctl_args_wait_semaphores args{};
    args.semaphores_addr = reinterpret_cast<uint64_t>(semaphores);
    args.count = count;
    args.timeout_ms = timeout_ms;
    args.wait_all = wait_all;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_WAIT_SEMAPHORES, args)) {
        DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(WAIT_SEMAPHORES) failed: %d", errno);
    }

    return args.status_return;
}

magma_status_t VirtioMagmaConnection::ExportSemaphore(magma_semaphore_t semaphore,
                                                      uint32_t* semaphore_handle_out)
{
    *semaphore_handle_out = 0;

    virtmagma_ioctl_args_export_semaphore args{};
    args.connection = connection_fd_;
    args.semaphore = semaphore;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_EXPORT_SEMAPHORE, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(EXPORT_SEMAPHORE) failed: %d", errno);
    }

    *semaphore_handle_out = args.semaphore_handle_out;
    return args.status_return;
}

magma_status_t VirtioMagmaConnection::ImportSemaphore(uint32_t semaphore_handle,
                                                      magma_semaphore_t* semaphore_out)
{
    *semaphore_out = 0;

    virtmagma_ioctl_args_import_semaphore args{};
    args.connection = connection_fd_;
    args.semaphore_handle = semaphore_handle;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_IMPORT_SEMAPHORE, &args)) {
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(IMPORT_SEMAPHORE) failed: %d", errno);
    }

    *semaphore_out = args.semaphore_out;
    return args.status_return;
}

magma_status_t VirtioMagmaConnection::ReadNotificationChannel(void* buffer, uint64_t buffer_size,
                                                              uint64_t* buffer_size_out)
{
    *buffer_size_out = 0;

    virtmagma_ioctl_args_read_notification_channel args{};
    args.connection = connection_fd_;
    args.buffer = reinterpret_cast<uint64_t>(buffer);
    args.buffer_size = buffer_size;
    if (ioctl(virtio_fd_, VIRTMAGMA_IOCTL_READ_NOTIFICATION_CHANNEL, &args)) {
        DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ioctl(READ_NOTIFICATION_CHANNEL) failed: %d", errno);
    }

    *buffer_size_out = args.buffer_size_out;
    return args.status_return;
}

VirtioMagmaConnection* VirtioMagmaConnection::cast(magma_connection_t connection)
{
    DASSERT(connection);
    DASSERT(connection->magic_ == kMagic);
    return static_cast<VirtioMagmaConnection*>(connection);
}

uint32_t VirtioMagmaConnection::Handshake(int32_t file_descriptor)
{
    if (fcntl(file_descriptor, F_GETFD) == -1) {
        DMESSAGE("Invalid file descriptor: %d", errno);
        return 0;
    }

    virtmagma_ioctl_args_handshake handshake{};
    handshake.handshake_inout = VIRTMAGMA_HANDSHAKE_SEND;
    if (ioctl(file_descriptor, VIRTMAGMA_IOCTL_HANDSHAKE, &handshake)) {
        DMESSAGE("ioctl(HANDSHAKE) failed: %d", errno);
        return 0;
    }

    if (handshake.handshake_inout != VIRTMAGMA_HANDSHAKE_RECV) {
        DMESSAGE("Handshake failed: 0x%08X", handshake.handshake_inout);
        return 0;
    }

    return handshake.version_out;
}

} // namespace magma
