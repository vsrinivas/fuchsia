// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "magma_sysmem.h"

#include "virtio_magma_connection.h"

bool magma_write_driver_to_filesystem(int32_t file_descriptor)
{
    return magma::VirtioMagmaConnection::WriteDriverToFilesystem(file_descriptor);
}

magma_status_t magma_query(int32_t file_descriptor, uint64_t id, uint64_t* value_out)
{
    return magma::VirtioMagmaConnection::Query(file_descriptor, id, value_out);
}

magma_status_t magma_create_connection(int32_t file_descriptor, magma_connection_t* connection_out)
{
    *connection_out = magma::VirtioMagmaConnection::Create(file_descriptor).release();
    return MAGMA_STATUS_OK;
}

magma_status_t magma_create_connection2(int32_t file_descriptor, magma_connection_t* connection_out)
{
    *connection_out = magma::VirtioMagmaConnection::Create(file_descriptor).release();
    return MAGMA_STATUS_OK;
}

void magma_release_connection(magma_connection_t connection)
{
    // Re-acquire ownership of the connection.
    std::unique_ptr<magma::VirtioMagmaConnection> conn(
        magma::VirtioMagmaConnection::cast(connection));
    conn->Release();
}

magma_status_t magma_get_error(magma_connection_t connection)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->GetError();
}

void magma_create_context(magma_connection_t connection, uint32_t* context_id_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    conn->CreateContext(context_id_out);
}

void magma_release_context(magma_connection_t connection, uint32_t context_id)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    conn->ReleaseContext(context_id);
}

magma_status_t magma_create_buffer(magma_connection_t connection, uint64_t size, uint64_t* size_out,
                                   magma_buffer_t* buffer_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->CreateBuffer(size, size_out, buffer_out);
}

void magma_release_buffer(magma_connection_t connection, magma_buffer_t buffer)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    conn->ReleaseBuffer(buffer);
}

uint64_t magma_get_buffer_id(magma_buffer_t buffer)
{
    // TODO(msandy): implement
    return 0;
}

uint64_t magma_get_buffer_size(magma_buffer_t buffer)
{
    // TODO(msandy): implement
    return 0;
}

magma_status_t magma_clean_cache(magma_buffer_t buffer, uint64_t offset, uint64_t size,
                                 magma_cache_operation_t operation)
{
    // TODO(msandy): implement
    return MAGMA_STATUS_OK;
}

magma_status_t magma_set_cache_policy(magma_buffer_t buffer, magma_cache_policy_t policy)
{
    // TODO(msandy): implement
    return MAGMA_STATUS_OK;
}

magma_status_t magma_map(magma_connection_t connection, magma_buffer_t buffer, void** addr_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->Map(buffer, addr_out);
}

magma_status_t magma_map_aligned(magma_connection_t connection, magma_buffer_t buffer,
                                 uint64_t alignment, void** addr_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->MapAligned(buffer, alignment, addr_out);
}

magma_status_t magma_map_specific(magma_connection_t connection, magma_buffer_t buffer,
                                  uint64_t addr)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->MapSpecific(buffer, addr);
}

magma_status_t magma_unmap(magma_connection_t connection, magma_buffer_t buffer)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->Unmap(buffer);
}

void magma_map_buffer_gpu(magma_connection_t connection, magma_buffer_t buffer,
                          uint64_t page_offset, uint64_t page_count, uint64_t gpu_va,
                          uint64_t map_flags)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->MapBufferGpu(buffer, page_offset, page_count, gpu_va, map_flags);
}

void magma_unmap_buffer_gpu(magma_connection_t connection, magma_buffer_t buffer, uint64_t gpu_va)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    conn->UnmapBufferGpu(buffer, gpu_va);
}

void magma_commit_buffer(magma_connection_t connection, magma_buffer_t buffer,
                         uint64_t page_offset, uint64_t page_count)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    conn->CommitBuffer(buffer, page_offset, page_count);
}

magma_status_t magma_export(magma_connection_t connection, magma_buffer_t buffer,
                            uint32_t* buffer_handle_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->Export(buffer, buffer_handle_out);
}

magma_status_t magma_import(magma_connection_t connection, uint32_t buffer_handle,
                            magma_buffer_t* buffer_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->Import(buffer_handle, buffer_out);
}

magma_status_t magma_create_command_buffer(magma_connection_t connection, uint64_t size,
                                           magma_buffer_t* buffer_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->CreateCommandBuffer(size, buffer_out);
}

void magma_release_command_buffer(magma_connection_t connection, magma_buffer_t command_buffer)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    conn->ReleaseCommandBuffer(command_buffer);
}

void magma_submit_command_buffer(magma_connection_t connection, magma_buffer_t command_buffer,
                                 uint32_t context_id)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    conn->SubmitCommandBuffer(command_buffer, context_id);
}

void magma_execute_immediate_commands(magma_connection_t connection, uint32_t context_id,
                                      uint64_t command_count,
                                      magma_system_inline_command_buffer* command_buffers)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    conn->ExecuteImmediateCommands(context_id, command_count, command_buffers);
}

magma_status_t magma_create_semaphore(magma_connection_t connection,
                                      magma_semaphore_t* semaphore_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->CreateSemaphore(semaphore_out);
}

void magma_release_semaphore(magma_connection_t connection, magma_semaphore_t semaphore)
{
    // TODO(msandy): implement
}

uint64_t magma_get_semaphore_id(magma_semaphore_t semaphore)
{
    // TODO(msandy): implement
    return 0;
}

void magma_signal_semaphore(magma_semaphore_t semaphore)
{
    // TODO(msandy): implement
}

void magma_reset_semaphore(magma_semaphore_t semaphore)
{
    // TODO(msandy): implement
}

magma_status_t magma_wait_semaphores(const magma_semaphore_t* semaphores, uint32_t count,
                                     uint64_t timeout_ms, magma_bool_t wait_all)
{
    // TODO(msandy): implement
    return MAGMA_STATUS_OK;
}

magma_status_t magma_export_semaphore(magma_connection_t connection, magma_semaphore_t semaphore,
                                      uint32_t* semaphore_handle_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->ExportSemaphore(semaphore, semaphore_handle_out);
}

magma_status_t magma_import_semaphore(magma_connection_t connection, uint32_t semaphore_handle,
                                      magma_semaphore_t* semaphore_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->ImportSemaphore(semaphore_handle, semaphore_out);
}

magma_status_t magma_sysmem_connection_create(magma_sysmem_connection_t* connection_out)
{
    // TODO(msandy): implement
    return MAGMA_STATUS_OK;
}

void magma_sysmem_connection_release(magma_sysmem_connection_t connection)
{
    // TODO(msandy): implement
}

magma_status_t magma_wait_notification_channel(magma_connection_t connection, int64_t timeout_ns)
{
    // TODO(msandy): implement
    return MAGMA_STATUS_OK;
}

magma_status_t magma_read_notification_channel(magma_connection_t connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out)
{
    auto conn = magma::VirtioMagmaConnection::cast(connection);
    return conn->ReadNotificationChannel(buffer, buffer_size, buffer_size_out);
}
