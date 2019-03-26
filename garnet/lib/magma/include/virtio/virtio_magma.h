// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_INCLUDE_VIRTIO_VIRTIO_MAGMA_H_
#define GARNET_LIB_MAGMA_INCLUDE_VIRTIO_VIRTIO_MAGMA_H_

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

enum virtio_magma_ctrl_type {
    /* magma commands */
    VIRTIO_MAGMA_CMD_GET_DRIVER = 0x0400,
    VIRTIO_MAGMA_CMD_QUERY,
    VIRTIO_MAGMA_CMD_CREATE_CONNECTION,
    VIRTIO_MAGMA_CMD_RELEASE_CONNECTION,
    VIRTIO_MAGMA_CMD_GET_ERROR,
    VIRTIO_MAGMA_CMD_CREATE_CONTEXT,
    VIRTIO_MAGMA_CMD_RELEASE_CONTEXT,
    VIRTIO_MAGMA_CMD_CREATE_BUFFER,
    VIRTIO_MAGMA_CMD_RELEASE_BUFFER,
    VIRTIO_MAGMA_CMD_GET_BUFFER_ID,
    VIRTIO_MAGMA_CMD_GET_BUFFER_SIZE,
    VIRTIO_MAGMA_CMD_CLEAN_CACHE,
    VIRTIO_MAGMA_CMD_SET_CACHE_POLICY,
    VIRTIO_MAGMA_CMD_MAP,
    VIRTIO_MAGMA_CMD_MAP_ALIGNED,
    VIRTIO_MAGMA_CMD_MAP_SPECIFIC,
    VIRTIO_MAGMA_CMD_UNMAP,
    VIRTIO_MAGMA_CMD_MAP_BUFFER_GPU,
    VIRTIO_MAGMA_CMD_UNMAP_BUFFER_GPU,
    VIRTIO_MAGMA_CMD_COMMIT_BUFFER,
    VIRTIO_MAGMA_CMD_EXPORT,
    VIRTIO_MAGMA_CMD_IMPORT,
    VIRTIO_MAGMA_CMD_CREATE_COMMAND_BUFFER,
    VIRTIO_MAGMA_CMD_RELEASE_COMMAND_BUFFER,
    VIRTIO_MAGMA_CMD_SUBMIT_COMMAND_BUFFER,
    VIRTIO_MAGMA_CMD_EXECUTE_IMMEDIATE_COMMANDS,
    VIRTIO_MAGMA_CMD_CREATE_SEMAPHORE,
    VIRTIO_MAGMA_CMD_RELEASE_SEMAPHORE,
    VIRTIO_MAGMA_CMD_GET_SEMAPHORE_ID,
    VIRTIO_MAGMA_CMD_SIGNAL_SEMAPHORE,
    VIRTIO_MAGMA_CMD_RESET_SEMAPHORE,
    VIRTIO_MAGMA_CMD_WAIT_SEMAPHORES,
    VIRTIO_MAGMA_CMD_EXPORT_SEMAPHORE,
    VIRTIO_MAGMA_CMD_IMPORT_SEMAPHORE,
    VIRTIO_MAGMA_CMD_READ_NOTIFICATION_CHANNEL,
    /* magma success responses */
    VIRTIO_MAGMA_RESP_GET_DRIVER = 0x1180,
    VIRTIO_MAGMA_RESP_QUERY,
    VIRTIO_MAGMA_RESP_CREATE_CONNECTION,
    VIRTIO_MAGMA_RESP_RELEASE_CONNECTION,
    VIRTIO_MAGMA_RESP_GET_ERROR,
    VIRTIO_MAGMA_RESP_CREATE_CONTEXT,
    VIRTIO_MAGMA_RESP_RELEASE_CONTEXT,
    VIRTIO_MAGMA_RESP_CREATE_BUFFER,
    VIRTIO_MAGMA_RESP_RELEASE_BUFFER,
    VIRTIO_MAGMA_RESP_GET_BUFFER_ID,
    VIRTIO_MAGMA_RESP_GET_BUFFER_SIZE,
    VIRTIO_MAGMA_RESP_CLEAN_CACHE,
    VIRTIO_MAGMA_RESP_SET_CACHE_POLICY,
    VIRTIO_MAGMA_RESP_MAP,
    VIRTIO_MAGMA_RESP_MAP_ALIGNED,
    VIRTIO_MAGMA_RESP_MAP_SPECIFIC,
    VIRTIO_MAGMA_RESP_UNMAP,
    VIRTIO_MAGMA_RESP_MAP_BUFFER_GPU,
    VIRTIO_MAGMA_RESP_UNMAP_BUFFER_GPU,
    VIRTIO_MAGMA_RESP_COMMIT_BUFFER,
    VIRTIO_MAGMA_RESP_EXPORT,
    VIRTIO_MAGMA_RESP_IMPORT,
    VIRTIO_MAGMA_RESP_CREATE_COMMAND_BUFFER,
    VIRTIO_MAGMA_RESP_RELEASE_COMMAND_BUFFER,
    VIRTIO_MAGMA_RESP_SUBMIT_COMMAND_BUFFER,
    VIRTIO_MAGMA_RESP_EXECUTE_IMMEDIATE_COMMANDS,
    VIRTIO_MAGMA_RESP_CREATE_SEMAPHORE,
    VIRTIO_MAGMA_RESP_RELEASE_SEMAPHORE,
    VIRTIO_MAGMA_RESP_GET_SEMAPHORE_ID,
    VIRTIO_MAGMA_RESP_SIGNAL_SEMAPHORE,
    VIRTIO_MAGMA_RESP_RESET_SEMAPHORE,
    VIRTIO_MAGMA_RESP_WAIT_SEMAPHORES,
    VIRTIO_MAGMA_RESP_EXPORT_SEMAPHORE,
    VIRTIO_MAGMA_RESP_IMPORT_SEMAPHORE,
    VIRTIO_MAGMA_RESP_READ_NOTIFICATION_CHANNEL,
    /* magma error responses */
    VIRTIO_MAGMA_RESP_ERR_UNIMPLEMENTED = 0x1280,
    VIRTIO_MAGMA_RESP_ERR_INTERNAL,
    VIRTIO_MAGMA_RESP_ERR_HOST_DISCONNECTED,
    VIRTIO_MAGMA_RESP_ERR_OUT_OF_MEMORY,
    VIRTIO_MAGMA_RESP_ERR_INVALID_COMMAND,
    VIRTIO_MAGMA_RESP_ERR_INVALID_ARGUMENT,
} __PACKED;

typedef struct virtio_magma_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
} __PACKED virtio_magma_ctrl_hdr_t;

typedef struct virtio_magma_get_driver {
    virtio_magma_ctrl_hdr_t hdr;
    uint32_t page_size;
} __PACKED virtio_magma_get_driver_t;

typedef struct virtio_magma_get_driver_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t pfn;
    uint64_t size;
} __PACKED virtio_magma_get_driver_resp_t;

typedef struct virtio_magma_query {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t field_id;
} __PACKED virtio_magma_query_t;

typedef struct virtio_magma_query_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t field_value_out;
    uint32_t status_return;
} __PACKED virtio_magma_query_resp_t;

typedef struct virtio_magma_create_connection {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_create_connection_t;

typedef struct virtio_magma_create_connection_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection_return;
} __PACKED virtio_magma_create_connection_resp_t;

typedef struct virtio_magma_release_connection {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
} __PACKED virtio_magma_release_connection_t;

typedef struct virtio_magma_release_connection_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_release_connection_resp_t;

typedef struct virtio_magma_get_error {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
} __PACKED virtio_magma_get_error_t;

typedef struct virtio_magma_get_error_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint32_t status_return;
} __PACKED virtio_magma_get_error_resp_t;

typedef struct virtio_magma_create_context {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
} __PACKED virtio_magma_create_context_t;

typedef struct virtio_magma_create_context_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint32_t context_id_out;
} __PACKED virtio_magma_create_context_resp_t;

typedef struct virtio_magma_release_context {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint32_t context_id;
} __PACKED virtio_magma_release_context_t;

typedef struct virtio_magma_release_context_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_release_context_resp_t;

typedef struct virtio_magma_create_buffer {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t size;
} __PACKED virtio_magma_create_buffer_t;

typedef struct virtio_magma_create_buffer_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t size_out;
    uint64_t buffer_out;
    uint32_t status_return;
} __PACKED virtio_magma_create_buffer_resp_t;

typedef struct virtio_magma_release_buffer {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
} __PACKED virtio_magma_release_buffer_t;

typedef struct virtio_magma_release_buffer_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_release_buffer_resp_t;

typedef struct virtio_magma_get_buffer_id {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t buffer;
} __PACKED virtio_magma_get_buffer_id_t;

typedef struct virtio_magma_get_buffer_id_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t id_return;
} __PACKED virtio_magma_get_buffer_id_resp_t;

typedef struct virtio_magma_get_buffer_size {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t buffer;
} __PACKED virtio_magma_get_buffer_size_t;

typedef struct virtio_magma_get_buffer_size_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t size_return;
} __PACKED virtio_magma_get_buffer_size_resp_t;

typedef struct virtio_magma_clean_cache {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t buffer;
    uint64_t offset;
    uint64_t size;
    uint32_t operation;
} __PACKED virtio_magma_clean_cache_t;

typedef struct virtio_magma_clean_cache_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint32_t status_return;
} __PACKED virtio_magma_clean_cache_resp_t;

typedef struct virtio_magma_set_cache_policy {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t buffer;
    uint32_t policy;
} __PACKED virtio_magma_set_cache_policy_t;

typedef struct virtio_magma_set_cache_policy_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint32_t status_return;
} __PACKED virtio_magma_set_cache_policy_resp_t;

typedef struct virtio_magma_map {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
} __PACKED virtio_magma_map_t;

typedef struct virtio_magma_map_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t addr_out;
    uint32_t status_return;
} __PACKED virtio_magma_map_resp_t;

typedef struct virtio_magma_map_aligned {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
    uint64_t alignment;
} __PACKED virtio_magma_map_aligned_t;

typedef struct virtio_magma_map_aligned_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t addr_out;
    uint32_t status_return;
} __PACKED virtio_magma_map_aligned_resp_t;

typedef struct virtio_magma_map_specific {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
    uint64_t addr;
} __PACKED virtio_magma_map_specific_t;

typedef struct virtio_magma_map_specific_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint32_t status_return;
} __PACKED virtio_magma_map_specific_resp_t;

typedef struct virtio_magma_unmap {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
} __PACKED virtio_magma_unmap_t;

typedef struct virtio_magma_unmap_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint32_t status_return;
} __PACKED virtio_magma_unmap_resp_t;

typedef struct virtio_magma_map_buffer_gpu {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
    uint64_t page_offset;
    uint64_t page_count;
    uint64_t gpu_va;
    uint64_t map_flags;
} __PACKED virtio_magma_map_buffer_gpu_t;

typedef struct virtio_magma_map_buffer_gpu_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_map_buffer_gpu_resp_t;

typedef struct virtio_magma_unmap_buffer_gpu {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
    uint64_t gpu_va;
} __PACKED virtio_magma_unmap_buffer_gpu_t;

typedef struct virtio_magma_unmap_buffer_gpu_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_unmap_buffer_gpu_resp_t;

typedef struct virtio_magma_commit_buffer {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
    uint64_t page_offset;
    uint64_t page_count;
} __PACKED virtio_magma_commit_buffer_t;

typedef struct virtio_magma_commit_buffer_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_commit_buffer_resp_t;

typedef struct virtio_magma_export {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
} __PACKED virtio_magma_export_t;

typedef struct virtio_magma_export_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint32_t buffer_handle_out;
    uint32_t status_return;
} __PACKED virtio_magma_export_resp_t;

typedef struct virtio_magma_import {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
} __PACKED virtio_magma_import_t;

typedef struct virtio_magma_import_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t buffer_out;
    uint32_t buffer_handle;
    uint32_t status_return;
} __PACKED virtio_magma_import_resp_t;

typedef struct virtio_magma_create_command_buffer {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t size;
} __PACKED virtio_magma_create_command_buffer_t;

typedef struct virtio_magma_create_command_buffer_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t buffer_out;
    uint32_t status_return;
} __PACKED virtio_magma_create_command_buffer_resp_t;

typedef struct virtio_magma_release_command_buffer {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t command_buffer;
} __PACKED virtio_magma_release_command_buffer_t;

typedef struct virtio_magma_release_command_buffer_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_release_command_buffer_resp_t;

typedef struct virtio_magma_submit_command_buffer {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t command_buffer;
    uint32_t context_id;
} __PACKED virtio_magma_submit_command_buffer_t;

typedef struct virtio_magma_submit_command_buffer_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_submit_command_buffer_resp_t;

typedef struct virtio_magma_execute_immediate_commands {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t command_count;
    uint64_t commands; // magma_system_inline_command_buffer[command_count]
    uint32_t context_id;
} __PACKED virtio_magma_execute_immediate_commands_t;

typedef struct virtio_magma_execute_immediate_commands_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_execute_immediate_commands_resp_t;

typedef struct virtio_magma_create_semaphore {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
} __PACKED virtio_magma_create_semaphore_t;

typedef struct virtio_magma_create_semaphore_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t semaphore_out;
    uint32_t status_return;
} __PACKED virtio_magma_create_semaphore_resp_t;

typedef struct virtio_magma_release_semaphore {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t semaphore;
} __PACKED virtio_magma_release_semaphore_t;

typedef struct virtio_magma_release_semaphore_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_release_semaphore_resp_t;

typedef struct virtio_magma_get_semaphore_id {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t semaphore;
} __PACKED virtio_magma_get_semaphore_id_t;

typedef struct virtio_magma_get_semaphore_id_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t id_return;
} __PACKED virtio_magma_get_semaphore_id_resp_t;

typedef struct virtio_magma_signal_semaphore {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t semaphore;
} __PACKED virtio_magma_signal_semaphore_t;

typedef struct virtio_magma_signal_semaphore_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_signal_semaphore_resp_t;

typedef struct virtio_magma_reset_semaphore {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t semaphore;
} __PACKED virtio_magma_reset_semaphore_t;

typedef struct virtio_magma_reset_semaphore_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_reset_semaphore_resp_t;

typedef struct virtio_magma_wait_semaphores {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t timeout_ms;
    uint64_t semaphores; // magma_semaphore_t[count]
    uint32_t count;
    uint32_t status_return;
    uint8_t wait_all;
} __PACKED virtio_magma_wait_semaphores_t;

typedef struct virtio_magma_wait_semaphores_resp {
    virtio_magma_ctrl_hdr_t hdr;
} __PACKED virtio_magma_wait_semaphores_resp_t;

typedef struct virtio_magma_export_semaphore {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t semaphore;
    uint64_t connection;
} __PACKED virtio_magma_export_semaphore_t;

typedef struct virtio_magma_export_semaphore_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint32_t semaphore_handle_out;
    uint32_t status_return;
} __PACKED virtio_magma_export_semaphore_resp_t;

typedef struct virtio_magma_import_semaphore {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint32_t semaphore_handle;
} __PACKED virtio_magma_import_semaphore_t;

typedef struct virtio_magma_import_semaphore_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t semaphore_out;
    uint32_t status_return;
} __PACKED virtio_magma_import_semaphore_resp_t;

typedef struct virtio_magma_read_notification_channel {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t connection;
    uint64_t buffer;
    uint64_t buffer_size;
} __PACKED virtio_magma_read_notification_channel_t;

typedef struct virtio_magma_read_notification_channel_resp {
    virtio_magma_ctrl_hdr_t hdr;
    uint64_t buffer_size_out;
    uint32_t status_return;
} __PACKED virtio_magma_read_notification_channel_resp_t;

__END_CDECLS

#endif // GARNET_LIB_MAGMA_INCLUDE_VIRTIO_VIRTIO_MAGMA_H_
