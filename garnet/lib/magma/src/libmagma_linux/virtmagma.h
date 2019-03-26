// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _LINUX_VIRTMAGMA_H
#define _LINUX_VIRTMAGMA_H

#include <asm/ioctl.h>
#include <linux/types.h>

#define VIRTMAGMA_IOCTL_BASE	 'm'
#define VIRTMAGMA_IO(nr)	 _IO(VIRTMAGMA_IOCTL_BASE, nr)
#define VIRTMAGMA_IOR(nr, type)	 _IOR(VIRTMAGMA_IOCTL_BASE, nr, type)
#define VIRTMAGMA_IOW(nr, type)	 _IOW(VIRTMAGMA_IOCTL_BASE, nr, type)
#define VIRTMAGMA_IOWR(nr, type) _IOWR(VIRTMAGMA_IOCTL_BASE, nr, type)
#define VIRTMAGMA_MAKE_VERSION(major, minor, patch) \
	(((major) << 24) | ((minor) << 12) | (patch))
#define VIRTMAGMA_GET_VERSION(version, major, minor, patch) (\
	(major = ((version) >> 24)), \
	(minor = ((version) >> 12) & 0x3FF), \
	(patch = (version) & 0x3FF), (version))

#define VIRTMAGMA_HANDSHAKE_SEND 0x46434853
#define VIRTMAGMA_HANDSHAKE_RECV 0x474F4F47
#define VIRTMAGMA_VERSION VIRTMAGMA_MAKE_VERSION(0,1,0)
struct virtmagma_ioctl_args_handshake {
	__u32 handshake_inout;
	__u32 version_out;
};

/* TODO(MA-520): implement virtio-magma */
struct virtmagma_ioctl_args_get_driver {
	__s32 unused;
};

struct virtmagma_ioctl_args_query {
	__u64 id;
	__u64 value_out;
	__u32 status_return;
};

struct virtmagma_ioctl_args_create_connection {
	__s32 connection_return;
};

struct virtmagma_ioctl_args_release_connection {
	__s32 connection;
};

struct virtmagma_ioctl_args_get_error {
	__s32 connection;
	__u32 status_return;
};

struct virtmagma_ioctl_args_create_context {
	__s32 connection;
	__u32 context_id_out;
};

struct virtmagma_ioctl_args_release_context {
	__s32 connection;
	__u32 context_id;
};

struct virtmagma_ioctl_args_create_buffer {
	__u64 size;
	__u64 size_out;
	__u64 buffer_out;
	__s32 connection;
	__u32 status_return;
};

struct virtmagma_ioctl_args_release_buffer {
	__u64 buffer;
	__s32 connection;
};

struct virtmagma_ioctl_args_get_buffer_id {
	__u64 buffer;
	__u64 id_return;
};

struct virtmagma_ioctl_args_get_buffer_size {
	__u64 buffer;
	__u64 size_return;
};

struct virtmagma_ioctl_args_clean_cache {
	__u64 buffer;
	__u64 offset;
	__u64 size;
	__u32 operation;
	__u32 status_return;
};

struct virtmagma_ioctl_args_set_cache_policy {
	__u64 buffer;
	__u32 policy;
	__u32 status_return;
};

struct virtmagma_ioctl_args_map {
	__u64 buffer;
	__u64 addr_out;
	__s32 connection;
	__u32 status_return;
};

struct virtmagma_ioctl_args_map_aligned {
	__u64 buffer;
	__u64 alignment;
	__u64 addr_out;
	__s32 connection;
	__u32 status_return;
};

struct virtmagma_ioctl_args_map_specific {
	__u64 buffer;
	__u64 addr;
	__s32 connection;
	__u32 status_return;
};

struct virtmagma_ioctl_args_unmap {
	__u64 buffer;
	__s32 connection;
	__u32 status_return;
};

struct virtmagma_ioctl_args_map_buffer_gpu {
	__u64 buffer;
	__u64 page_offset;
	__u64 page_count;
	__u64 gpu_va;
	__u64 map_flags;
	__s32 connection;
};

struct virtmagma_ioctl_args_unmap_buffer_gpu {
	__u64 buffer;
	__u64 gpu_va;
	__s32 connection;
};

struct virtmagma_ioctl_args_commit_buffer {
	__u64 buffer;
	__u64 page_offset;
	__u64 page_count;
	__s32 connection;
};

struct virtmagma_ioctl_args_export {
	__u64 buffer;
	__u32 buffer_handle_out;
	__u32 status_return;
	__s32 connection;
};

struct virtmagma_ioctl_args_import {
	__u64 buffer_out;
	__u32 buffer_handle;
	__u32 status_return;
	__s32 connection;
};

struct virtmagma_ioctl_args_create_command_buffer {
	__u64 size;
	__u64 buffer_out;
	__u32 status_return;
	__s32 connection;
};

struct virtmagma_ioctl_args_release_command_buffer {
	__u64 command_buffer;
	__s32 connection;
};

struct virtmagma_ioctl_args_submit_command_buffer {
	__u64 command_buffer;
	__u32 context_id;
	__s32 connection;
};

struct virtmagma_ioctl_args_execute_immediate_commands {
	__u64 command_count;
	__u64 commands_addr; /* magma_system_inline_command_buffer[command_count] */
	__u32 context_id;
	__s32 connection;
};

struct virtmagma_ioctl_args_create_semaphore {
	__u64 semaphore_out;
	__s32 connection;
	__u32 status_return;
};

struct virtmagma_ioctl_args_release_semaphore {
	__u64 semaphore;
	__s32 connection;
};

struct virtmagma_ioctl_args_get_semaphore_id {
	__u64 semaphore;
	__u64 id_return;
};

struct virtmagma_ioctl_args_signal_semaphore {
	__u64 semaphore;
};

struct virtmagma_ioctl_args_reset_semaphore {
	__u64 semaphore;
};

struct virtmagma_ioctl_args_wait_semaphores {
	__u64 timeout_ms;
	__u64 semaphores_addr; /* magma_semaphore_t[count] */
	__u32 count;
	__u32 status_return;
	__u8 wait_all;
};

struct virtmagma_ioctl_args_export_semaphore {
	__u64 semaphore;
	__s32 connection;
	__u32 semaphore_handle_out;
	__u32 status_return;
};

struct virtmagma_ioctl_args_import_semaphore {
	__u64 semaphore_out;
	__s32 connection;
	__u32 semaphore_handle;
	__u32 status_return;
};

struct virtmagma_ioctl_args_get_notification_channel_fd {
	__s32 connection;
	__s32 fd_return;
};

struct virtmagma_ioctl_args_read_notification_channel {
	__u64 buffer;
	__u64 buffer_size;
	__u64 buffer_size_out;
	__s32 connection;
	__u32 status_return;
};

#define VIRTMAGMA_IOCTL_HANDSHAKE VIRTMAGMA_IOWR(0x00, struct virtmagma_ioctl_args_handshake)
#define VIRTMAGMA_IOCTL_GET_DRIVER VIRTMAGMA_IOWR(0x01, struct virtmagma_ioctl_args_get_driver)
#define VIRTMAGMA_IOCTL_QUERY VIRTMAGMA_IOWR(0x02, struct virtmagma_ioctl_args_query)
#define VIRTMAGMA_IOCTL_CREATE_CONNECTION VIRTMAGMA_IOWR(0x03, struct virtmagma_ioctl_args_create_connection)
#define VIRTMAGMA_IOCTL_RELEASE_CONNECTION VIRTMAGMA_IOWR(0x04, struct virtmagma_ioctl_args_release_connection)
#define VIRTMAGMA_IOCTL_GET_ERROR VIRTMAGMA_IOWR(0x05, struct virtmagma_ioctl_args_get_error)
#define VIRTMAGMA_IOCTL_CREATE_CONTEXT VIRTMAGMA_IOWR(0x06, struct virtmagma_ioctl_args_create_context)
#define VIRTMAGMA_IOCTL_RELEASE_CONTEXT VIRTMAGMA_IOWR(0x07, struct virtmagma_ioctl_args_release_context)
#define VIRTMAGMA_IOCTL_CREATE_BUFFER VIRTMAGMA_IOWR(0x08, struct virtmagma_ioctl_args_create_buffer)
#define VIRTMAGMA_IOCTL_RELEASE_BUFFER VIRTMAGMA_IOWR(0x09, struct virtmagma_ioctl_args_release_buffer)
#define VIRTMAGMA_IOCTL_GET_BUFFER_ID VIRTMAGMA_IOWR(0x0A, struct virtmagma_ioctl_args_get_buffer_id)
#define VIRTMAGMA_IOCTL_GET_BUFFER_SIZE VIRTMAGMA_IOWR(0x0B, struct virtmagma_ioctl_args_get_buffer_size)
#define VIRTMAGMA_IOCTL_CLEAN_CACHE VIRTMAGMA_IOWR(0x0C, struct virtmagma_ioctl_args_clean_cache)
#define VIRTMAGMA_IOCTL_SET_CACHE_POLICY VIRTMAGMA_IOWR(0x0D, struct virtmagma_ioctl_args_set_cache_policy)
#define VIRTMAGMA_IOCTL_MAP VIRTMAGMA_IOWR(0x0E, struct virtmagma_ioctl_args_map)
#define VIRTMAGMA_IOCTL_MAP_ALIGNED VIRTMAGMA_IOWR(0x0F, struct virtmagma_ioctl_args_map_aligned)
#define VIRTMAGMA_IOCTL_MAP_SPECIFIC VIRTMAGMA_IOWR(0x10, struct virtmagma_ioctl_args_map_specific)
#define VIRTMAGMA_IOCTL_UNMAP VIRTMAGMA_IOWR(0x11, struct virtmagma_ioctl_args_unmap)
#define VIRTMAGMA_IOCTL_MAP_BUFFER_GPU VIRTMAGMA_IOWR(0x12, struct virtmagma_ioctl_args_map_buffer_gpu)
#define VIRTMAGMA_IOCTL_UNMAP_BUFFER_GPU VIRTMAGMA_IOWR(0x13, struct virtmagma_ioctl_args_unmap_buffer_gpu)
#define VIRTMAGMA_IOCTL_COMMIT_BUFFER VIRTMAGMA_IOWR(0x14, struct virtmagma_ioctl_args_commit_buffer)
#define VIRTMAGMA_IOCTL_EXPORT VIRTMAGMA_IOWR(0x15, struct virtmagma_ioctl_args_export)
#define VIRTMAGMA_IOCTL_IMPORT VIRTMAGMA_IOWR(0x16, struct virtmagma_ioctl_args_import)
#define VIRTMAGMA_IOCTL_CREATE_COMMAND_BUFFER VIRTMAGMA_IOWR(0x17, struct virtmagma_ioctl_args_create_command_buffer)
#define VIRTMAGMA_IOCTL_RELEASE_COMMAND_BUFFER VIRTMAGMA_IOWR(0x18, struct virtmagma_ioctl_args_release_command_buffer)
#define VIRTMAGMA_IOCTL_SUBMIT_COMMAND_BUFFER VIRTMAGMA_IOWR(0x19, struct virtmagma_ioctl_args_submit_command_buffer)
#define VIRTMAGMA_IOCTL_EXECUTE_IMMEDIATE_COMMANDS VIRTMAGMA_IOWR(0x1A, struct virtmagma_ioctl_args_execute_immediate_commands)
#define VIRTMAGMA_IOCTL_CREATE_SEMAPHORE VIRTMAGMA_IOWR(0x1B, struct virtmagma_ioctl_args_create_semaphore)
#define VIRTMAGMA_IOCTL_RELEASE_SEMAPHORE VIRTMAGMA_IOWR(0x1C, struct virtmagma_ioctl_args_release_semaphore)
#define VIRTMAGMA_IOCTL_GET_SEMAPHORE_ID VIRTMAGMA_IOWR(0x1D, struct virtmagma_ioctl_args_get_semaphore_id)
#define VIRTMAGMA_IOCTL_SIGNAL_SEMAPHORE VIRTMAGMA_IOWR(0x1E, struct virtmagma_ioctl_args_signal_semaphore)
#define VIRTMAGMA_IOCTL_RESET_SEMAPHORE VIRTMAGMA_IOWR(0x1F, struct virtmagma_ioctl_args_reset_semaphore)
#define VIRTMAGMA_IOCTL_WAIT_SEMAPHORES VIRTMAGMA_IOWR(0x20, struct virtmagma_ioctl_args_wait_semaphores)
#define VIRTMAGMA_IOCTL_EXPORT_SEMAPHORE VIRTMAGMA_IOWR(0x21, struct virtmagma_ioctl_args_export_semaphore)
#define VIRTMAGMA_IOCTL_IMPORT_SEMAPHORE VIRTMAGMA_IOWR(0x22, struct virtmagma_ioctl_args_import_semaphore)
#define VIRTMAGMA_IOCTL_GET_NOTIFICATION_CHANNEL_FD VIRTMAGMA_IOWR(0x23, struct virtmagma_ioctl_args_get_notification_channel_fd)
#define VIRTMAGMA_IOCTL_READ_NOTIFICATION_CHANNEL VIRTMAGMA_IOWR(0x24, struct virtmagma_ioctl_args_read_notification_channel)

#endif /* _LINUX_VIRTMAGMA_H */
