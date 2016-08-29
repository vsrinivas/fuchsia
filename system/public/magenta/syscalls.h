// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <stdbool.h>

#include <magenta/syscalls-types.h>

#ifdef __cplusplus
extern "C" {
#endif

// define all of the syscalls from the syscall list header.
// user space syscall vaneer routines are all prefixed with mx_
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) extern ret mx_##name(args);
#define MAGENTA_SYSCALL_DEF_WITH_ATTRS(nargs64, nargs32, n, ret, name, attrs, args...) extern ret mx_##name(args) __attribute__(attrs);

#include <magenta/syscalls.inc>

// compatibility wrappers for renamed syscalls
//TODO: deprecate and then remove


static inline mx_status_t mx_message_pipe_create(mx_handle_t* out_handles, uint32_t flags) {
    return mx_msgpipe_create(out_handles, flags);
}
static inline mx_status_t mx_message_read(mx_handle_t handle, void* bytes, uint32_t* num_bytes,
                                          mx_handle_t* handles, uint32_t* num_handles, uint32_t flags) {
    return mx_msgpipe_read(handle, bytes, num_bytes, handles, num_handles, flags);
}
static inline mx_status_t mx_message_write(mx_handle_t handle, const void* bytes, uint32_t num_bytes,
                                           const mx_handle_t* handles, uint32_t num_handles, uint32_t flags) {
    return mx_msgpipe_write(handle, bytes, num_bytes, handles, num_handles, flags);
}

static inline mx_handle_t mx_interrupt_event_create(mx_handle_t handle, uint32_t vector, uint32_t flags) {
    return mx_interrupt_create(handle, vector, flags);
}
static inline mx_status_t mx_interrupt_event_complete(mx_handle_t handle) {
    return mx_interrupt_complete(handle);
}
static inline mx_status_t mx_interrupt_event_wait(mx_handle_t handle) {
    return mx_interrupt_wait(handle);
}

static inline mx_status_t mx_process_vm_map(mx_handle_t proc_handle, mx_handle_t vmo_handle,
                                            uint64_t offset, mx_size_t len, uintptr_t* ptr, uint32_t flags) {
    return mx_process_map_vm(proc_handle, vmo_handle, offset, len, ptr, flags);
}
static inline mx_status_t mx_process_vm_unmap(mx_handle_t proc_handle, uintptr_t address, mx_size_t len) {
    return mx_process_unmap_vm(proc_handle, address, len);
}
static inline mx_status_t mx_process_vm_protect(mx_handle_t proc_handle, uintptr_t address, mx_size_t len, uint32_t prot) {
    return mx_process_protect_vm(proc_handle, address, len, prot);
}

static inline mx_handle_t mx_vm_object_create(uint64_t size) {
    return mx_vmo_create(size);
}
static inline mx_ssize_t mx_vm_object_read(mx_handle_t handle, void *data, uint64_t offset, mx_size_t len) {
    return mx_vmo_read(handle, data, offset, len);
}
static inline mx_ssize_t mx_vm_object_write(mx_handle_t handle, const void *data, uint64_t offset, mx_size_t len) {
    return mx_vmo_write(handle, data, offset, len);
}
static inline mx_status_t mx_vm_object_get_size(mx_handle_t handle, uint64_t* size) {
    return mx_vmo_get_size(handle, size);
}
static inline mx_status_t mx_vm_object_set_size(mx_handle_t handle, uint64_t size) {
    return mx_vmo_set_size(handle, size);
}

static inline mx_handle_t mx_io_port_create(uint32_t options) {
    return mx_port_create(options);
}
static inline mx_status_t mx_io_port_queue(mx_handle_t handle, const void* packet, mx_size_t size) {
    return mx_port_queue(handle, packet, size);
}
static inline mx_status_t mx_io_port_wait(mx_handle_t handle, void* packet, mx_size_t size) {
    return mx_port_wait(handle, packet, size);
}
static inline mx_status_t mx_io_port_bind(mx_handle_t handle, uint64_t key, mx_handle_t source, mx_signals_t signals) {
    return mx_port_bind(handle, key, source, signals);
}


static inline mx_handle_t mx_data_pipe_create(uint32_t options, mx_size_t element_size, mx_size_t capacity, mx_handle_t* handle) {
    return mx_datapipe_create(options, element_size, capacity, handle);
}
static inline mx_ssize_t mx_data_pipe_write(mx_handle_t handle, uint32_t flags, mx_size_t requested, const void* buffer) {
    return mx_datapipe_write(handle, flags, requested, buffer);
}
static inline mx_ssize_t mx_data_pipe_read(mx_handle_t handle, uint32_t flags, mx_size_t requested, void* buffer) {
    return mx_datapipe_read(handle, flags, requested, buffer);
}
static inline mx_ssize_t mx_data_pipe_begin_write(mx_handle_t handle, uint32_t flags, mx_size_t requested, uintptr_t* buffer) {
    return mx_datapipe_begin_write(handle, flags, requested, buffer);
}
static inline mx_status_t mx_data_pipe_end_write(mx_handle_t handle, mx_size_t written) {
    return mx_datapipe_end_write(handle, written);
}
static inline mx_ssize_t mx_data_pipe_begin_read(mx_handle_t handle, uint32_t flags, mx_size_t requested, uintptr_t* buffer) {
    return mx_datapipe_begin_read(handle, flags, requested, buffer);
}
static inline mx_status_t mx_data_pipe_end_read(mx_handle_t handle, mx_size_t read) {
    return mx_datapipe_end_read(handle, read);
}

static inline mx_handle_t mx_wait_set_create(void) {
    return mx_waitset_create();
}
static inline mx_status_t mx_wait_set_add(mx_handle_t waitset_handle, mx_handle_t handle, mx_signals_t signals, uint64_t cookie) {
    return mx_waitset_add(waitset_handle, handle, signals, cookie);
}
static inline mx_status_t mx_wait_set_remove(mx_handle_t waitset_handle, uint64_t cookie) {
    return mx_waitset_remove(waitset_handle, cookie);
}
static inline mx_status_t mx_wait_set_wait(mx_handle_t waitset_handle, mx_time_t timeout, uint32_t* num_results,
                             mx_wait_set_result_t* results, uint32_t* max_results) {
    return mx_waitset_wait(waitset_handle, timeout, num_results, results, max_results);
}

#ifdef __cplusplus
}
#endif
