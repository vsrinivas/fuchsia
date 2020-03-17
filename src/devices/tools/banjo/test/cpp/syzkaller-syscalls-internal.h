// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.syscalls banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_clock_get, Apiclock_get,
        zx_status_t (C::*)(zx_clock_t clock_id, zx_time_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_clock_get_new, Apiclock_get_new,
        zx_status_t (C::*)(zx_clock_t clock_id, zx_time_t* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_clock_get_monotonic, Apiclock_get_monotonic,
        zx_time_t (C::*)());

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_nanosleep, Apinanosleep,
        zx_status_t (C::*)(zx_time_t deadline));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_ticks_get, Apiticks_get,
        zx_ticks_t (C::*)());

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_ticks_per_second, Apiticks_per_second,
        zx_ticks_t (C::*)());

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_deadline_after, Apideadline_after,
        zx_time_t (C::*)(zx_duration_t nanoseconds));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_clock_adjust, Apiclock_adjust,
        zx_status_t (C::*)(zx::resource handle, zx_clock_t clock_id, int64_t offset));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_system_get_dcache_line_size, Apisystem_get_dcache_line_size,
        uint32_t (C::*)());

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_system_get_num_cpus, Apisystem_get_num_cpus,
        uint32_t (C::*)());

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_system_get_version, Apisystem_get_version,
        zx_status_t (C::*)(const char* version, size_t version_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_system_get_physmem, Apisystem_get_physmem,
        uint64_t (C::*)());

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_system_get_features, Apisystem_get_features,
        zx_status_t (C::*)(uint32_t kind, uint32_t* out_features));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_system_get_event, Apisystem_get_event,
        zx_status_t (C::*)(zx::job root_job, uint32_t kind, zx::event* out_event));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_cache_flush, Apicache_flush,
        zx_status_t (C::*)(const void addr[size], size_t size, uint32_t options));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle_close, Apihandle_close,
        zx_status_t (C::*)(zx::handle handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle_close_many, Apihandle_close_many,
        zx_status_t (C::*)(const zx::handle handles[num_handles], size_t num_handles));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle_duplicate, Apihandle_duplicate,
        zx_status_t (C::*)(zx::handle handle, zx_rights_t rights, zx::handle* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle_replace, Apihandle_replace,
        zx_status_t (C::*)(zx::handle handle, zx_rights_t rights, zx::handle* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_wait_one, Apiobject_wait_one,
        zx_status_t (C::*)(zx::handle handle, zx_signals_t signals, zx_time_t deadline, zx_signals_t* out_observed));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_wait_many, Apiobject_wait_many,
        zx_status_t (C::*)(const zx_wait_item_t items[count], size_t count, zx_time_t deadline));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_wait_async, Apiobject_wait_async,
        zx_status_t (C::*)(zx::handle handle, zx::port port, uint64_t key, zx_signals_t signals, uint32_t options));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_signal, Apiobject_signal,
        zx_status_t (C::*)(zx::handle handle, uint32_t clear_mask, uint32_t set_mask));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_signal_peer, Apiobject_signal_peer,
        zx_status_t (C::*)(zx::handle handle, uint32_t clear_mask, uint32_t set_mask));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_get_property, Apiobject_get_property,
        zx_status_t (C::*)(zx::handle handle, uint32_t property, const void value[value_size], size_t value_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_set_property, Apiobject_set_property,
        zx_status_t (C::*)(zx::handle handle, uint32_t property, const void value[value_size], size_t value_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_get_info, Apiobject_get_info,
        zx_status_t (C::*)(zx::handle handle, uint32_t topic, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual, size_t* out_avail));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_get_child, Apiobject_get_child,
        zx_status_t (C::*)(zx::handle handle, zx_koid_t koid, zx_rights_t rights, zx::handle* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_object_set_profile, Apiobject_set_profile,
        zx_status_t (C::*)(zx::thread handle, zx::profile profile, uint32_t options));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_socket_create, Apisocket_create,
        zx_status_t (C::*)(uint32_t options, zx::socket* out_out0, zx::socket* out_out1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_socket_write, Apisocket_write,
        zx_status_t (C::*)(zx::socket handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_socket_read, Apisocket_read,
        zx_status_t (C::*)(zx::socket handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_socket_share, Apisocket_share,
        zx_status_t (C::*)(zx::socket handle, zx::socket socket_to_share));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_socket_accept, Apisocket_accept,
        zx_status_t (C::*)(zx::socket handle, zx::socket* out_out_socket));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_socket_shutdown, Apisocket_shutdown,
        zx_status_t (C::*)(zx::socket handle, uint32_t options));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_thread_exit, Apithread_exit,
        void (C::*)());

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_thread_create, Apithread_create,
        zx_status_t (C::*)(zx::process process, const char* name, size_t name_size, uint32_t options, zx::thread* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_thread_start, Apithread_start,
        zx_status_t (C::*)(zx::thread handle, zx_vaddr_t thread_entry, zx_vaddr_t stack, size_t arg1, size_t arg2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_thread_read_state, Apithread_read_state,
        zx_status_t (C::*)(zx::thread handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_thread_write_state, Apithread_write_state,
        zx_status_t (C::*)(zx::thread handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_process_exit, Apiprocess_exit,
        void (C::*)(int64_t retcode));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_process_create, Apiprocess_create,
        zx_status_t (C::*)(zx::job job, const char* name, size_t name_size, uint32_t options, zx::process* out_proc_handle, zx::vmar* out_vmar_handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_process_start, Apiprocess_start,
        zx_status_t (C::*)(zx::process handle, zx::thread thread, zx_vaddr_t entry, zx_vaddr_t stack, zx::handle arg1, size_t arg2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_process_read_memory, Apiprocess_read_memory,
        zx_status_t (C::*)(zx::process handle, zx_vaddr_t vaddr, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_process_write_memory, Apiprocess_write_memory,
        zx_status_t (C::*)(zx::process handle, zx_vaddr_t vaddr, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_job_create, Apijob_create,
        zx_status_t (C::*)(zx::job parent_job, uint32_t options, zx::job* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_job_set_policy, Apijob_set_policy,
        zx_status_t (C::*)(zx::job handle, uint32_t options, uint32_t topic, const void policy[count], uint32_t count));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_task_suspend, Apitask_suspend,
        zx_status_t (C::*)(zx::handle handle, zx::handle* out_token));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_task_suspend_token, Apitask_suspend_token,
        zx_status_t (C::*)(zx::handle handle, zx::handle* out_token));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_task_create_exception_channel, Apitask_create_exception_channel,
        zx_status_t (C::*)(zx::handle handle, uint32_t options, zx::channel* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_task_kill, Apitask_kill,
        zx_status_t (C::*)(zx::handle handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_exception_get_thread, Apiexception_get_thread,
        zx_status_t (C::*)(zx::handle handle, zx::thread* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_exception_get_process, Apiexception_get_process,
        zx_status_t (C::*)(zx::handle handle, zx::process* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_event_create, Apievent_create,
        zx_status_t (C::*)(uint32_t options, zx::event* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_eventpair_create, Apieventpair_create,
        zx_status_t (C::*)(uint32_t options, zx::eventpair* out_out0, zx::eventpair* out_out1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_futex_wait, Apifutex_wait,
        zx_status_t (C::*)(const int32_t value_ptr[1], int32_t current_value, zx::handle new_futex_owner, zx_time_t deadline));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_futex_wake, Apifutex_wake,
        zx_status_t (C::*)(const int32_t value_ptr[1], uint32_t wake_count));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_futex_requeue, Apifutex_requeue,
        zx_status_t (C::*)(const int32_t value_ptr[1], uint32_t wake_count, int32_t current_value, const int32_t requeue_ptr[1], uint32_t requeue_count, zx::handle new_requeue_owner));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_futex_wake_single_owner, Apifutex_wake_single_owner,
        zx_status_t (C::*)(const int32_t value_ptr[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_futex_requeue_single_owner, Apifutex_requeue_single_owner,
        zx_status_t (C::*)(const int32_t value_ptr[1], int32_t current_value, const int32_t requeue_ptr[1], uint32_t requeue_count, zx::handle new_requeue_owner));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_futex_get_owner, Apifutex_get_owner,
        zx_status_t (C::*)(const int32_t value_ptr[1], const zx_koid_t koid[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_port_create, Apiport_create,
        zx_status_t (C::*)(uint32_t options, zx::port* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_port_queue, Apiport_queue,
        zx_status_t (C::*)(zx::port handle, const zx_port_packet_t packet[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_port_wait, Apiport_wait,
        zx_status_t (C::*)(zx::port handle, zx_time_t deadline, const zx_port_packet_t packet[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_port_cancel, Apiport_cancel,
        zx_status_t (C::*)(zx::port handle, zx::handle source, uint64_t key));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_timer_create, Apitimer_create,
        zx_status_t (C::*)(uint32_t options, zx_clock_t clock_id, zx::timer* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_timer_set, Apitimer_set,
        zx_status_t (C::*)(zx::timer handle, zx_time_t deadline, zx_duration_t slack));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_timer_cancel, Apitimer_cancel,
        zx_status_t (C::*)(zx::timer handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_create, Apivmo_create,
        zx_status_t (C::*)(uint64_t size, uint32_t options, zx::vmo* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_read, Apivmo_read,
        zx_status_t (C::*)(zx::vmo handle, const void buffer[buffer_size], uint64_t offset, size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_write, Apivmo_write,
        zx_status_t (C::*)(zx::vmo handle, const void buffer[buffer_size], uint64_t offset, size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_get_size, Apivmo_get_size,
        zx_status_t (C::*)(zx::vmo handle, uint64_t* out_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_set_size, Apivmo_set_size,
        zx_status_t (C::*)(zx::vmo handle, uint64_t size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_op_range, Apivmo_op_range,
        zx_status_t (C::*)(zx::vmo handle, uint32_t op, uint64_t offset, uint64_t size, const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_create_child, Apivmo_create_child,
        zx_status_t (C::*)(zx::vmo handle, uint32_t options, uint64_t offset, uint64_t size, zx::vmo* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_set_cache_policy, Apivmo_set_cache_policy,
        zx_status_t (C::*)(zx::vmo handle, uint32_t cache_policy));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_replace_as_executable, Apivmo_replace_as_executable,
        zx_status_t (C::*)(zx::vmo handle, zx::resource vmex, zx::vmo* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmar_allocate, Apivmar_allocate,
        zx_status_t (C::*)(zx::vmar parent_vmar, zx_vm_option_t options, uint64_t offset, uint64_t size, zx::vmar* out_child_vmar, zx_vaddr_t* out_child_addr));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmar_destroy, Apivmar_destroy,
        zx_status_t (C::*)(zx::vmar handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmar_map, Apivmar_map,
        zx_status_t (C::*)(zx::vmar handle, zx_vm_option_t options, uint64_t vmar_offset, zx::vmo vmo, uint64_t vmo_offset, uint64_t len, zx_vaddr_t* out_mapped_addr));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmar_unmap, Apivmar_unmap,
        zx_status_t (C::*)(zx::vmo handle, zx_vaddr_t addr, uint64_t len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmar_protect, Apivmar_protect,
        zx_status_t (C::*)(zx::vmo handle, zx_vm_option_t options, zx_vaddr_t addr, uint64_t len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_cprng_draw_once, Apicprng_draw_once,
        zx_status_t (C::*)(const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_cprng_draw, Apicprng_draw,
        void (C::*)(const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_cprng_add_entropy, Apicprng_add_entropy,
        zx_status_t (C::*)(const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_fifo_create, Apififo_create,
        zx_status_t (C::*)(size_t elem_count, size_t elem_size, uint32_t options, zx::fifo* out_out0, zx::fifo* out_out1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_fifo_read, Apififo_read,
        zx_status_t (C::*)(zx::fifo handle, size_t elem_size, const void data[N], size_t count, size_t* out_actual_count));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_fifo_write, Apififo_write,
        zx_status_t (C::*)(zx::fifo handle, size_t elem_size, const void data[N], size_t count, size_t* out_actual_count));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_profile_create, Apiprofile_create,
        zx_status_t (C::*)(zx::job root_job, uint32_t options, const zx_profile_info_t profile[1], zx::profile* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmar_unmap_handle_close_thread_exit, Apivmar_unmap_handle_close_thread_exit,
        zx_status_t (C::*)(zx::vmar vmar_handle, zx_vaddr_t addr, size_t size, zx::handle close_handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_futex_wake_handle_close_thread_exit, Apifutex_wake_handle_close_thread_exit,
        void (C::*)(const int32_t value_ptr[1], uint32_t wake_count, int32_t new_value, zx::handle close_handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_debuglog_create, Apidebuglog_create,
        zx_status_t (C::*)(zx::resource resource, uint32_t options, zx::debuglog* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_debuglog_write, Apidebuglog_write,
        zx_status_t (C::*)(zx::debuglog handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_debuglog_read, Apidebuglog_read,
        zx_status_t (C::*)(zx::debuglog handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_ktrace_read, Apiktrace_read,
        zx_status_t (C::*)(zx::resource handle, const void data[data_size], uint32_t offset, size_t data_size, size_t* out_actual));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_ktrace_control, Apiktrace_control,
        zx_status_t (C::*)(zx::resource handle, uint32_t action, uint32_t options, const void ptr[action]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_ktrace_write, Apiktrace_write,
        zx_status_t (C::*)(zx::resource handle, uint32_t id, uint32_t arg0, uint32_t arg1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_mtrace_control, Apimtrace_control,
        zx_status_t (C::*)(zx::resource handle, uint32_t kind, uint32_t action, uint32_t options, const void ptr[ptr_size], size_t ptr_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_debug_read, Apidebug_read,
        zx_status_t (C::*)(zx::resource handle, const char* buffer, const size_t buffer_size[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_debug_write, Apidebug_write,
        zx_status_t (C::*)(const char* buffer, size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_debug_send_command, Apidebug_send_command,
        zx_status_t (C::*)(zx::resource resource, const char* buffer, size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_interrupt_create, Apiinterrupt_create,
        zx_status_t (C::*)(zx::resource src_obj, uint32_t src_num, uint32_t options, zx::interrupt* out_out_handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_interrupt_bind, Apiinterrupt_bind,
        zx_status_t (C::*)(zx::interrupt handle, zx::port port_handle, uint64_t key, uint32_t options));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_interrupt_wait, Apiinterrupt_wait,
        zx_status_t (C::*)(zx::interrupt handle, zx_time_t* out_out_timestamp));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_interrupt_destroy, Apiinterrupt_destroy,
        zx_status_t (C::*)(zx::interrupt handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_interrupt_ack, Apiinterrupt_ack,
        zx_status_t (C::*)(zx::interrupt handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_interrupt_trigger, Apiinterrupt_trigger,
        zx_status_t (C::*)(zx::interrupt handle, uint32_t options, zx_time_t timestamp));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_interrupt_bind_vcpu, Apiinterrupt_bind_vcpu,
        zx_status_t (C::*)(zx::interrupt handle, zx::vcpu vcpu, uint32_t options));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_ioports_request, Apiioports_request,
        zx_status_t (C::*)(zx::resource resource, uint16_t io_addr, uint32_t len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_create_contiguous, Apivmo_create_contiguous,
        zx_status_t (C::*)(zx::bti bti, size_t size, uint32_t alignment_log2, zx::vmo* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vmo_create_physical, Apivmo_create_physical,
        zx_status_t (C::*)(zx::vmo resource, zx_paddr_t paddr, size_t size, zx::vmo* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_iommu_create, Apiiommu_create,
        zx_status_t (C::*)(zx::resource resource, uint32_t type, const void desc[desc_size], size_t desc_size, zx::iommu* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bti_create, Apibti_create,
        zx_status_t (C::*)(zx::iommu iommu, uint32_t options, uint64_t bti_id, zx::bti* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bti_pin, Apibti_pin,
        zx_status_t (C::*)(zx::bti handle, uint32_t options, zx::vmo vmo, uint64_t offset, uint64_t size, const zx_paddr_t addrs[addrs_count], size_t addrs_count, zx::pmt* out_pmt));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bti_release_quarantine, Apibti_release_quarantine,
        zx_status_t (C::*)(zx::bti handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pmt_unpin, Apipmt_unpin,
        zx_status_t (C::*)(zx::pmt handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_framebuffer_get_info, Apiframebuffer_get_info,
        zx_status_t (C::*)(zx::resource resource, uint32_t* out_format, uint32_t* out_width, uint32_t* out_height, uint32_t* out_stride));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_framebuffer_set_range, Apiframebuffer_set_range,
        zx_status_t (C::*)(zx::resource resource, zx::vmo vmo, uint32_t len, uint32_t format, uint32_t width, uint32_t height, uint32_t stride));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_get_nth_device, Apipci_get_nth_device,
        zx_status_t (C::*)(zx::resource handle, uint32_t index, zx_pcie_device_info_t* out_out_info, zx::handle* out_out_handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_enable_bus_master, Apipci_enable_bus_master,
        zx_status_t (C::*)(zx::handle handle, bool enable));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_reset_device, Apipci_reset_device,
        zx_status_t (C::*)(zx::handle handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_config_read, Apipci_config_read,
        zx_status_t (C::*)(zx::handle handle, uint16_t offset, size_t width, const uint32_t out_val[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_config_write, Apipci_config_write,
        zx_status_t (C::*)(zx::handle handle, uint16_t offset, size_t width, uint32_t val));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_cfg_pio_rw, Apipci_cfg_pio_rw,
        zx_status_t (C::*)(zx::resource handle, uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, const uint32_t val[1], size_t width, bool write));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_get_bar, Apipci_get_bar,
        zx_status_t (C::*)(zx::handle handle, uint32_t bar_num, const zx_pci_bar_t out_bar[1], zx::handle* out_out_handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_map_interrupt, Apipci_map_interrupt,
        zx_status_t (C::*)(zx::handle handle, int32_t which_irq, zx::handle* out_out_handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_query_irq_mode, Apipci_query_irq_mode,
        zx_status_t (C::*)(zx::handle handle, uint32_t mode, uint32_t* out_out_max_irqs));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_set_irq_mode, Apipci_set_irq_mode,
        zx_status_t (C::*)(zx::handle handle, uint32_t mode, uint32_t requested_irq_count));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_init, Apipci_init,
        zx_status_t (C::*)(zx::resource handle, const zx_pci_init_arg_t init_buf[len], uint32_t len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pci_add_subtract_io_range, Apipci_add_subtract_io_range,
        zx_status_t (C::*)(zx::resource handle, bool mmio, uint64_t base, uint64_t len, bool add));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pc_firmware_tables, Apipc_firmware_tables,
        zx_status_t (C::*)(zx::resource handle, zx_paddr_t* out_acpi_rsdp, zx_paddr_t* out_smbios));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_smc_call, Apismc_call,
        zx_status_t (C::*)(zx::handle handle, const zx_smc_parameters_t parameters[1], zx_smc_result_t* out_out_smc_result));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_resource_create, Apiresource_create,
        zx_status_t (C::*)(zx::resource parent_rsrc, uint32_t options, uint64_t base, size_t size, const char* name, size_t name_size, zx::resource* out_resource_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_guest_create, Apiguest_create,
        zx_status_t (C::*)(zx::resource resource, uint32_t options, zx::guest* out_guest_handle, zx::vmar* out_vmar_handle));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_guest_set_trap, Apiguest_set_trap,
        zx_status_t (C::*)(zx::guest handle, uint32_t kind, zx_vaddr_t addr, size_t size, zx::port port_handle, uint64_t key));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vcpu_create, Apivcpu_create,
        zx_status_t (C::*)(zx::guest guest, uint32_t options, zx_vaddr_t entry, zx::vcpu* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vcpu_resume, Apivcpu_resume,
        zx_status_t (C::*)(zx::vcpu handle, zx_port_packet_t* out_packet));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vcpu_interrupt, Apivcpu_interrupt,
        zx_status_t (C::*)(zx::vcpu handle, uint32_t vector));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vcpu_read_state, Apivcpu_read_state,
        zx_status_t (C::*)(zx::handle handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vcpu_write_state, Apivcpu_write_state,
        zx_status_t (C::*)(zx::handle handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_system_mexec, Apisystem_mexec,
        zx_status_t (C::*)(zx::resource resource, zx::vmo kernel_vmo, zx::vmo bootimage_vmo));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_system_mexec_payload_get, Apisystem_mexec_payload_get,
        zx_status_t (C::*)(zx::resource resource, const void buffer[buffer_size], size_t buffer_size));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_system_powerctl, Apisystem_powerctl,
        zx_status_t (C::*)(zx::resource resource, uint32_t cmd, const zx_system_powerctl_arg_t arg[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pager_create, Apipager_create,
        zx_status_t (C::*)(uint32_t options, zx::pager* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pager_create_vmo, Apipager_create_vmo,
        zx_status_t (C::*)(zx::pager pager, uint32_t options, zx::port port, uint64_t key, uint64_t size, zx::vmo* out_out));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pager_detach_vmo, Apipager_detach_vmo,
        zx_status_t (C::*)(zx::pager pager, zx::vmo vmo));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_pager_supply_pages, Apipager_supply_pages,
        zx_status_t (C::*)(zx::pager pager, zx::vmo pager_vmo, uint64_t offset, uint64_t length, zx::vmo aux_vmo, uint64_t aux_offset));


template <typename D>
constexpr void CheckApiProtocolSubclass() {
    static_assert(internal::has_api_protocol_clock_get<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiclock_get(zx_clock_t clock_id, zx_time_t* out_out);");

    static_assert(internal::has_api_protocol_clock_get_new<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiclock_get_new(zx_clock_t clock_id, zx_time_t* out_out);");

    static_assert(internal::has_api_protocol_clock_get_monotonic<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_time_t Apiclock_get_monotonic();");

    static_assert(internal::has_api_protocol_nanosleep<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apinanosleep(zx_time_t deadline);");

    static_assert(internal::has_api_protocol_ticks_get<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_ticks_t Apiticks_get();");

    static_assert(internal::has_api_protocol_ticks_per_second<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_ticks_t Apiticks_per_second();");

    static_assert(internal::has_api_protocol_deadline_after<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_time_t Apideadline_after(zx_duration_t nanoseconds);");

    static_assert(internal::has_api_protocol_clock_adjust<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiclock_adjust(zx::resource handle, zx_clock_t clock_id, int64_t offset);");

    static_assert(internal::has_api_protocol_system_get_dcache_line_size<D>::value,
        "ApiProtocol subclasses must implement "
        "uint32_t Apisystem_get_dcache_line_size();");

    static_assert(internal::has_api_protocol_system_get_num_cpus<D>::value,
        "ApiProtocol subclasses must implement "
        "uint32_t Apisystem_get_num_cpus();");

    static_assert(internal::has_api_protocol_system_get_version<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisystem_get_version(const char* version, size_t version_size);");

    static_assert(internal::has_api_protocol_system_get_physmem<D>::value,
        "ApiProtocol subclasses must implement "
        "uint64_t Apisystem_get_physmem();");

    static_assert(internal::has_api_protocol_system_get_features<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisystem_get_features(uint32_t kind, uint32_t* out_features);");

    static_assert(internal::has_api_protocol_system_get_event<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisystem_get_event(zx::job root_job, uint32_t kind, zx::event* out_event);");

    static_assert(internal::has_api_protocol_cache_flush<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apicache_flush(const void addr[size], size_t size, uint32_t options);");

    static_assert(internal::has_api_protocol_handle_close<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apihandle_close(zx::handle handle);");

    static_assert(internal::has_api_protocol_handle_close_many<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apihandle_close_many(const zx::handle handles[num_handles], size_t num_handles);");

    static_assert(internal::has_api_protocol_handle_duplicate<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apihandle_duplicate(zx::handle handle, zx_rights_t rights, zx::handle* out_out);");

    static_assert(internal::has_api_protocol_handle_replace<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apihandle_replace(zx::handle handle, zx_rights_t rights, zx::handle* out_out);");

    static_assert(internal::has_api_protocol_object_wait_one<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_wait_one(zx::handle handle, zx_signals_t signals, zx_time_t deadline, zx_signals_t* out_observed);");

    static_assert(internal::has_api_protocol_object_wait_many<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_wait_many(const zx_wait_item_t items[count], size_t count, zx_time_t deadline);");

    static_assert(internal::has_api_protocol_object_wait_async<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_wait_async(zx::handle handle, zx::port port, uint64_t key, zx_signals_t signals, uint32_t options);");

    static_assert(internal::has_api_protocol_object_signal<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_signal(zx::handle handle, uint32_t clear_mask, uint32_t set_mask);");

    static_assert(internal::has_api_protocol_object_signal_peer<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_signal_peer(zx::handle handle, uint32_t clear_mask, uint32_t set_mask);");

    static_assert(internal::has_api_protocol_object_get_property<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_get_property(zx::handle handle, uint32_t property, const void value[value_size], size_t value_size);");

    static_assert(internal::has_api_protocol_object_set_property<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_set_property(zx::handle handle, uint32_t property, const void value[value_size], size_t value_size);");

    static_assert(internal::has_api_protocol_object_get_info<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_get_info(zx::handle handle, uint32_t topic, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual, size_t* out_avail);");

    static_assert(internal::has_api_protocol_object_get_child<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_get_child(zx::handle handle, zx_koid_t koid, zx_rights_t rights, zx::handle* out_out);");

    static_assert(internal::has_api_protocol_object_set_profile<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiobject_set_profile(zx::thread handle, zx::profile profile, uint32_t options);");

    static_assert(internal::has_api_protocol_socket_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisocket_create(uint32_t options, zx::socket* out_out0, zx::socket* out_out1);");

    static_assert(internal::has_api_protocol_socket_write<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisocket_write(zx::socket handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual);");

    static_assert(internal::has_api_protocol_socket_read<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisocket_read(zx::socket handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual);");

    static_assert(internal::has_api_protocol_socket_share<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisocket_share(zx::socket handle, zx::socket socket_to_share);");

    static_assert(internal::has_api_protocol_socket_accept<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisocket_accept(zx::socket handle, zx::socket* out_out_socket);");

    static_assert(internal::has_api_protocol_socket_shutdown<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisocket_shutdown(zx::socket handle, uint32_t options);");

    static_assert(internal::has_api_protocol_thread_exit<D>::value,
        "ApiProtocol subclasses must implement "
        "void Apithread_exit();");

    static_assert(internal::has_api_protocol_thread_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apithread_create(zx::process process, const char* name, size_t name_size, uint32_t options, zx::thread* out_out);");

    static_assert(internal::has_api_protocol_thread_start<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apithread_start(zx::thread handle, zx_vaddr_t thread_entry, zx_vaddr_t stack, size_t arg1, size_t arg2);");

    static_assert(internal::has_api_protocol_thread_read_state<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apithread_read_state(zx::thread handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_thread_write_state<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apithread_write_state(zx::thread handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_process_exit<D>::value,
        "ApiProtocol subclasses must implement "
        "void Apiprocess_exit(int64_t retcode);");

    static_assert(internal::has_api_protocol_process_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiprocess_create(zx::job job, const char* name, size_t name_size, uint32_t options, zx::process* out_proc_handle, zx::vmar* out_vmar_handle);");

    static_assert(internal::has_api_protocol_process_start<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiprocess_start(zx::process handle, zx::thread thread, zx_vaddr_t entry, zx_vaddr_t stack, zx::handle arg1, size_t arg2);");

    static_assert(internal::has_api_protocol_process_read_memory<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiprocess_read_memory(zx::process handle, zx_vaddr_t vaddr, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual);");

    static_assert(internal::has_api_protocol_process_write_memory<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiprocess_write_memory(zx::process handle, zx_vaddr_t vaddr, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual);");

    static_assert(internal::has_api_protocol_job_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apijob_create(zx::job parent_job, uint32_t options, zx::job* out_out);");

    static_assert(internal::has_api_protocol_job_set_policy<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apijob_set_policy(zx::job handle, uint32_t options, uint32_t topic, const void policy[count], uint32_t count);");

    static_assert(internal::has_api_protocol_task_suspend<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apitask_suspend(zx::handle handle, zx::handle* out_token);");

    static_assert(internal::has_api_protocol_task_suspend_token<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apitask_suspend_token(zx::handle handle, zx::handle* out_token);");

    static_assert(internal::has_api_protocol_task_create_exception_channel<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apitask_create_exception_channel(zx::handle handle, uint32_t options, zx::channel* out_out);");

    static_assert(internal::has_api_protocol_task_kill<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apitask_kill(zx::handle handle);");

    static_assert(internal::has_api_protocol_exception_get_thread<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiexception_get_thread(zx::handle handle, zx::thread* out_out);");

    static_assert(internal::has_api_protocol_exception_get_process<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiexception_get_process(zx::handle handle, zx::process* out_out);");

    static_assert(internal::has_api_protocol_event_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apievent_create(uint32_t options, zx::event* out_out);");

    static_assert(internal::has_api_protocol_eventpair_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apieventpair_create(uint32_t options, zx::eventpair* out_out0, zx::eventpair* out_out1);");

    static_assert(internal::has_api_protocol_futex_wait<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apifutex_wait(const int32_t value_ptr[1], int32_t current_value, zx::handle new_futex_owner, zx_time_t deadline);");

    static_assert(internal::has_api_protocol_futex_wake<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apifutex_wake(const int32_t value_ptr[1], uint32_t wake_count);");

    static_assert(internal::has_api_protocol_futex_requeue<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apifutex_requeue(const int32_t value_ptr[1], uint32_t wake_count, int32_t current_value, const int32_t requeue_ptr[1], uint32_t requeue_count, zx::handle new_requeue_owner);");

    static_assert(internal::has_api_protocol_futex_wake_single_owner<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apifutex_wake_single_owner(const int32_t value_ptr[1]);");

    static_assert(internal::has_api_protocol_futex_requeue_single_owner<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apifutex_requeue_single_owner(const int32_t value_ptr[1], int32_t current_value, const int32_t requeue_ptr[1], uint32_t requeue_count, zx::handle new_requeue_owner);");

    static_assert(internal::has_api_protocol_futex_get_owner<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apifutex_get_owner(const int32_t value_ptr[1], const zx_koid_t koid[1]);");

    static_assert(internal::has_api_protocol_port_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiport_create(uint32_t options, zx::port* out_out);");

    static_assert(internal::has_api_protocol_port_queue<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiport_queue(zx::port handle, const zx_port_packet_t packet[1]);");

    static_assert(internal::has_api_protocol_port_wait<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiport_wait(zx::port handle, zx_time_t deadline, const zx_port_packet_t packet[1]);");

    static_assert(internal::has_api_protocol_port_cancel<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiport_cancel(zx::port handle, zx::handle source, uint64_t key);");

    static_assert(internal::has_api_protocol_timer_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apitimer_create(uint32_t options, zx_clock_t clock_id, zx::timer* out_out);");

    static_assert(internal::has_api_protocol_timer_set<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apitimer_set(zx::timer handle, zx_time_t deadline, zx_duration_t slack);");

    static_assert(internal::has_api_protocol_timer_cancel<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apitimer_cancel(zx::timer handle);");

    static_assert(internal::has_api_protocol_vmo_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_create(uint64_t size, uint32_t options, zx::vmo* out_out);");

    static_assert(internal::has_api_protocol_vmo_read<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_read(zx::vmo handle, const void buffer[buffer_size], uint64_t offset, size_t buffer_size);");

    static_assert(internal::has_api_protocol_vmo_write<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_write(zx::vmo handle, const void buffer[buffer_size], uint64_t offset, size_t buffer_size);");

    static_assert(internal::has_api_protocol_vmo_get_size<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_get_size(zx::vmo handle, uint64_t* out_size);");

    static_assert(internal::has_api_protocol_vmo_set_size<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_set_size(zx::vmo handle, uint64_t size);");

    static_assert(internal::has_api_protocol_vmo_op_range<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_op_range(zx::vmo handle, uint32_t op, uint64_t offset, uint64_t size, const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_vmo_create_child<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_create_child(zx::vmo handle, uint32_t options, uint64_t offset, uint64_t size, zx::vmo* out_out);");

    static_assert(internal::has_api_protocol_vmo_set_cache_policy<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_set_cache_policy(zx::vmo handle, uint32_t cache_policy);");

    static_assert(internal::has_api_protocol_vmo_replace_as_executable<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_replace_as_executable(zx::vmo handle, zx::resource vmex, zx::vmo* out_out);");

    static_assert(internal::has_api_protocol_vmar_allocate<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmar_allocate(zx::vmar parent_vmar, zx_vm_option_t options, uint64_t offset, uint64_t size, zx::vmar* out_child_vmar, zx_vaddr_t* out_child_addr);");

    static_assert(internal::has_api_protocol_vmar_destroy<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmar_destroy(zx::vmar handle);");

    static_assert(internal::has_api_protocol_vmar_map<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmar_map(zx::vmar handle, zx_vm_option_t options, uint64_t vmar_offset, zx::vmo vmo, uint64_t vmo_offset, uint64_t len, zx_vaddr_t* out_mapped_addr);");

    static_assert(internal::has_api_protocol_vmar_unmap<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmar_unmap(zx::vmo handle, zx_vaddr_t addr, uint64_t len);");

    static_assert(internal::has_api_protocol_vmar_protect<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmar_protect(zx::vmo handle, zx_vm_option_t options, zx_vaddr_t addr, uint64_t len);");

    static_assert(internal::has_api_protocol_cprng_draw_once<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apicprng_draw_once(const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_cprng_draw<D>::value,
        "ApiProtocol subclasses must implement "
        "void Apicprng_draw(const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_cprng_add_entropy<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apicprng_add_entropy(const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_fifo_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apififo_create(size_t elem_count, size_t elem_size, uint32_t options, zx::fifo* out_out0, zx::fifo* out_out1);");

    static_assert(internal::has_api_protocol_fifo_read<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apififo_read(zx::fifo handle, size_t elem_size, const void data[N], size_t count, size_t* out_actual_count);");

    static_assert(internal::has_api_protocol_fifo_write<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apififo_write(zx::fifo handle, size_t elem_size, const void data[N], size_t count, size_t* out_actual_count);");

    static_assert(internal::has_api_protocol_profile_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiprofile_create(zx::job root_job, uint32_t options, const zx_profile_info_t profile[1], zx::profile* out_out);");

    static_assert(internal::has_api_protocol_vmar_unmap_handle_close_thread_exit<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmar_unmap_handle_close_thread_exit(zx::vmar vmar_handle, zx_vaddr_t addr, size_t size, zx::handle close_handle);");

    static_assert(internal::has_api_protocol_futex_wake_handle_close_thread_exit<D>::value,
        "ApiProtocol subclasses must implement "
        "void Apifutex_wake_handle_close_thread_exit(const int32_t value_ptr[1], uint32_t wake_count, int32_t new_value, zx::handle close_handle);");

    static_assert(internal::has_api_protocol_debuglog_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apidebuglog_create(zx::resource resource, uint32_t options, zx::debuglog* out_out);");

    static_assert(internal::has_api_protocol_debuglog_write<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apidebuglog_write(zx::debuglog handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_debuglog_read<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apidebuglog_read(zx::debuglog handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_ktrace_read<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiktrace_read(zx::resource handle, const void data[data_size], uint32_t offset, size_t data_size, size_t* out_actual);");

    static_assert(internal::has_api_protocol_ktrace_control<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiktrace_control(zx::resource handle, uint32_t action, uint32_t options, const void ptr[action]);");

    static_assert(internal::has_api_protocol_ktrace_write<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiktrace_write(zx::resource handle, uint32_t id, uint32_t arg0, uint32_t arg1);");

    static_assert(internal::has_api_protocol_mtrace_control<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apimtrace_control(zx::resource handle, uint32_t kind, uint32_t action, uint32_t options, const void ptr[ptr_size], size_t ptr_size);");

    static_assert(internal::has_api_protocol_debug_read<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apidebug_read(zx::resource handle, const char* buffer, const size_t buffer_size[1]);");

    static_assert(internal::has_api_protocol_debug_write<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apidebug_write(const char* buffer, size_t buffer_size);");

    static_assert(internal::has_api_protocol_debug_send_command<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apidebug_send_command(zx::resource resource, const char* buffer, size_t buffer_size);");

    static_assert(internal::has_api_protocol_interrupt_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiinterrupt_create(zx::resource src_obj, uint32_t src_num, uint32_t options, zx::interrupt* out_out_handle);");

    static_assert(internal::has_api_protocol_interrupt_bind<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiinterrupt_bind(zx::interrupt handle, zx::port port_handle, uint64_t key, uint32_t options);");

    static_assert(internal::has_api_protocol_interrupt_wait<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiinterrupt_wait(zx::interrupt handle, zx_time_t* out_out_timestamp);");

    static_assert(internal::has_api_protocol_interrupt_destroy<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiinterrupt_destroy(zx::interrupt handle);");

    static_assert(internal::has_api_protocol_interrupt_ack<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiinterrupt_ack(zx::interrupt handle);");

    static_assert(internal::has_api_protocol_interrupt_trigger<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiinterrupt_trigger(zx::interrupt handle, uint32_t options, zx_time_t timestamp);");

    static_assert(internal::has_api_protocol_interrupt_bind_vcpu<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiinterrupt_bind_vcpu(zx::interrupt handle, zx::vcpu vcpu, uint32_t options);");

    static_assert(internal::has_api_protocol_ioports_request<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiioports_request(zx::resource resource, uint16_t io_addr, uint32_t len);");

    static_assert(internal::has_api_protocol_vmo_create_contiguous<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_create_contiguous(zx::bti bti, size_t size, uint32_t alignment_log2, zx::vmo* out_out);");

    static_assert(internal::has_api_protocol_vmo_create_physical<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivmo_create_physical(zx::vmo resource, zx_paddr_t paddr, size_t size, zx::vmo* out_out);");

    static_assert(internal::has_api_protocol_iommu_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiiommu_create(zx::resource resource, uint32_t type, const void desc[desc_size], size_t desc_size, zx::iommu* out_out);");

    static_assert(internal::has_api_protocol_bti_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apibti_create(zx::iommu iommu, uint32_t options, uint64_t bti_id, zx::bti* out_out);");

    static_assert(internal::has_api_protocol_bti_pin<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apibti_pin(zx::bti handle, uint32_t options, zx::vmo vmo, uint64_t offset, uint64_t size, const zx_paddr_t addrs[addrs_count], size_t addrs_count, zx::pmt* out_pmt);");

    static_assert(internal::has_api_protocol_bti_release_quarantine<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apibti_release_quarantine(zx::bti handle);");

    static_assert(internal::has_api_protocol_pmt_unpin<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipmt_unpin(zx::pmt handle);");

    static_assert(internal::has_api_protocol_framebuffer_get_info<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiframebuffer_get_info(zx::resource resource, uint32_t* out_format, uint32_t* out_width, uint32_t* out_height, uint32_t* out_stride);");

    static_assert(internal::has_api_protocol_framebuffer_set_range<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiframebuffer_set_range(zx::resource resource, zx::vmo vmo, uint32_t len, uint32_t format, uint32_t width, uint32_t height, uint32_t stride);");

    static_assert(internal::has_api_protocol_pci_get_nth_device<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_get_nth_device(zx::resource handle, uint32_t index, zx_pcie_device_info_t* out_out_info, zx::handle* out_out_handle);");

    static_assert(internal::has_api_protocol_pci_enable_bus_master<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_enable_bus_master(zx::handle handle, bool enable);");

    static_assert(internal::has_api_protocol_pci_reset_device<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_reset_device(zx::handle handle);");

    static_assert(internal::has_api_protocol_pci_config_read<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_config_read(zx::handle handle, uint16_t offset, size_t width, const uint32_t out_val[1]);");

    static_assert(internal::has_api_protocol_pci_config_write<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_config_write(zx::handle handle, uint16_t offset, size_t width, uint32_t val);");

    static_assert(internal::has_api_protocol_pci_cfg_pio_rw<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_cfg_pio_rw(zx::resource handle, uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, const uint32_t val[1], size_t width, bool write);");

    static_assert(internal::has_api_protocol_pci_get_bar<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_get_bar(zx::handle handle, uint32_t bar_num, const zx_pci_bar_t out_bar[1], zx::handle* out_out_handle);");

    static_assert(internal::has_api_protocol_pci_map_interrupt<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_map_interrupt(zx::handle handle, int32_t which_irq, zx::handle* out_out_handle);");

    static_assert(internal::has_api_protocol_pci_query_irq_mode<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_query_irq_mode(zx::handle handle, uint32_t mode, uint32_t* out_out_max_irqs);");

    static_assert(internal::has_api_protocol_pci_set_irq_mode<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_set_irq_mode(zx::handle handle, uint32_t mode, uint32_t requested_irq_count);");

    static_assert(internal::has_api_protocol_pci_init<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_init(zx::resource handle, const zx_pci_init_arg_t init_buf[len], uint32_t len);");

    static_assert(internal::has_api_protocol_pci_add_subtract_io_range<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipci_add_subtract_io_range(zx::resource handle, bool mmio, uint64_t base, uint64_t len, bool add);");

    static_assert(internal::has_api_protocol_pc_firmware_tables<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipc_firmware_tables(zx::resource handle, zx_paddr_t* out_acpi_rsdp, zx_paddr_t* out_smbios);");

    static_assert(internal::has_api_protocol_smc_call<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apismc_call(zx::handle handle, const zx_smc_parameters_t parameters[1], zx_smc_result_t* out_out_smc_result);");

    static_assert(internal::has_api_protocol_resource_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiresource_create(zx::resource parent_rsrc, uint32_t options, uint64_t base, size_t size, const char* name, size_t name_size, zx::resource* out_resource_out);");

    static_assert(internal::has_api_protocol_guest_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiguest_create(zx::resource resource, uint32_t options, zx::guest* out_guest_handle, zx::vmar* out_vmar_handle);");

    static_assert(internal::has_api_protocol_guest_set_trap<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiguest_set_trap(zx::guest handle, uint32_t kind, zx_vaddr_t addr, size_t size, zx::port port_handle, uint64_t key);");

    static_assert(internal::has_api_protocol_vcpu_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivcpu_create(zx::guest guest, uint32_t options, zx_vaddr_t entry, zx::vcpu* out_out);");

    static_assert(internal::has_api_protocol_vcpu_resume<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivcpu_resume(zx::vcpu handle, zx_port_packet_t* out_packet);");

    static_assert(internal::has_api_protocol_vcpu_interrupt<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivcpu_interrupt(zx::vcpu handle, uint32_t vector);");

    static_assert(internal::has_api_protocol_vcpu_read_state<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivcpu_read_state(zx::handle handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_vcpu_write_state<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivcpu_write_state(zx::handle handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_system_mexec<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisystem_mexec(zx::resource resource, zx::vmo kernel_vmo, zx::vmo bootimage_vmo);");

    static_assert(internal::has_api_protocol_system_mexec_payload_get<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisystem_mexec_payload_get(zx::resource resource, const void buffer[buffer_size], size_t buffer_size);");

    static_assert(internal::has_api_protocol_system_powerctl<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisystem_powerctl(zx::resource resource, uint32_t cmd, const zx_system_powerctl_arg_t arg[1]);");

    static_assert(internal::has_api_protocol_pager_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipager_create(uint32_t options, zx::pager* out_out);");

    static_assert(internal::has_api_protocol_pager_create_vmo<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipager_create_vmo(zx::pager pager, uint32_t options, zx::port port, uint64_t key, uint64_t size, zx::vmo* out_out);");

    static_assert(internal::has_api_protocol_pager_detach_vmo<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipager_detach_vmo(zx::pager pager, zx::vmo vmo);");

    static_assert(internal::has_api_protocol_pager_supply_pages<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipager_supply_pages(zx::pager pager, zx::vmo pager_vmo, uint64_t offset, uint64_t length, zx::vmo aux_vmo, uint64_t aux_offset);");

}


} // namespace internal
} // namespace ddk
