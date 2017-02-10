// Copyright 2017 The Fuchsia Authors. All rights reserved.
// This is a GENERATED file. The license governing this file can be found in the LICENSE file.

extern mx_time_t mx_time_get(
    uint32_t clock_id) __attribute__((__leaf__));

extern mx_time_t _mx_time_get(
    uint32_t clock_id) __attribute__((__leaf__));

extern mx_status_t mx_nanosleep(
    mx_time_t nanoseconds) __attribute__((__leaf__));

extern mx_status_t _mx_nanosleep(
    mx_time_t nanoseconds) __attribute__((__leaf__));

extern uint64_t mx_ticks_get(void) __attribute__((__leaf__));

extern uint64_t _mx_ticks_get(void) __attribute__((__leaf__));

extern uint64_t mx_ticks_per_second(void) __attribute__((__leaf__)) __attribute__((const));

extern uint64_t _mx_ticks_per_second(void) __attribute__((__leaf__)) __attribute__((const));

extern uint32_t mx_num_cpus(void) __attribute__((__leaf__)) __attribute__((const));

extern uint32_t _mx_num_cpus(void) __attribute__((__leaf__)) __attribute__((const));

extern mx_status_t mx_version_get(
    char version[],
    uint32_t version_len) __attribute__((__leaf__));

extern mx_status_t _mx_version_get(
    char version[],
    uint32_t version_len) __attribute__((__leaf__));

extern mx_status_t mx_cache_flush(
    const void* addr,
    size_t len,
    uint32_t flags) __attribute__((__leaf__));

extern mx_status_t _mx_cache_flush(
    const void* addr,
    size_t len,
    uint32_t flags) __attribute__((__leaf__));

extern mx_status_t mx_handle_close(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t _mx_handle_close(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t mx_handle_duplicate(
    mx_handle_t handle,
    mx_rights_t rights,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_handle_duplicate(
    mx_handle_t handle,
    mx_rights_t rights,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_handle_replace(
    mx_handle_t handle,
    mx_rights_t rights,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_handle_replace(
    mx_handle_t handle,
    mx_rights_t rights,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_object_wait_one(
    mx_handle_t handle,
    mx_signals_t waitfor,
    mx_time_t timeout,
    mx_signals_t observed[1]) __attribute__((__leaf__));

extern mx_status_t _mx_object_wait_one(
    mx_handle_t handle,
    mx_signals_t waitfor,
    mx_time_t timeout,
    mx_signals_t observed[1]) __attribute__((__leaf__));

extern mx_status_t mx_object_wait_many(
    mx_wait_item_t items[],
    uint32_t count,
    mx_time_t timeout) __attribute__((__leaf__));

extern mx_status_t _mx_object_wait_many(
    mx_wait_item_t items[],
    uint32_t count,
    mx_time_t timeout) __attribute__((__leaf__));

extern mx_status_t mx_object_wait_async(
    mx_handle_t handle,
    mx_handle_t port_handle,
    uint64_t key,
    mx_signals_t signals,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t _mx_object_wait_async(
    mx_handle_t handle,
    mx_handle_t port_handle,
    uint64_t key,
    mx_signals_t signals,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t mx_object_signal(
    mx_handle_t handle,
    uint32_t clear_mask,
    uint32_t set_mask) __attribute__((__leaf__));

extern mx_status_t _mx_object_signal(
    mx_handle_t handle,
    uint32_t clear_mask,
    uint32_t set_mask) __attribute__((__leaf__));

extern mx_status_t mx_object_signal_peer(
    mx_handle_t handle,
    uint32_t clear_mask,
    uint32_t set_mask) __attribute__((__leaf__));

extern mx_status_t _mx_object_signal_peer(
    mx_handle_t handle,
    uint32_t clear_mask,
    uint32_t set_mask) __attribute__((__leaf__));

extern mx_status_t mx_object_get_property(
    mx_handle_t handle,
    uint32_t property,
    void* value,
    size_t size) __attribute__((__leaf__));

extern mx_status_t _mx_object_get_property(
    mx_handle_t handle,
    uint32_t property,
    void* value,
    size_t size) __attribute__((__leaf__));

extern mx_status_t mx_object_set_property(
    mx_handle_t handle,
    uint32_t property,
    const void* value,
    size_t size) __attribute__((__leaf__));

extern mx_status_t _mx_object_set_property(
    mx_handle_t handle,
    uint32_t property,
    const void* value,
    size_t size) __attribute__((__leaf__));

extern mx_status_t mx_object_get_info(
    mx_handle_t handle,
    uint32_t topic,
    void* buffer,
    size_t buffer_size,
    size_t actual_count[1],
    size_t avail_count[1]) __attribute__((__leaf__));

extern mx_status_t _mx_object_get_info(
    mx_handle_t handle,
    uint32_t topic,
    void* buffer,
    size_t buffer_size,
    size_t actual_count[1],
    size_t avail_count[1]) __attribute__((__leaf__));

extern mx_status_t mx_object_get_child(
    mx_handle_t handle,
    uint64_t koid,
    mx_rights_t rights,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_object_get_child(
    mx_handle_t handle,
    uint64_t koid,
    mx_rights_t rights,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_object_bind_exception_port(
    mx_handle_t object,
    mx_handle_t eport,
    uint64_t key,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t _mx_object_bind_exception_port(
    mx_handle_t object,
    mx_handle_t eport,
    uint64_t key,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t mx_channel_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]) __attribute__((__leaf__));

extern mx_status_t _mx_channel_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]) __attribute__((__leaf__));

extern mx_status_t mx_channel_read(
    mx_handle_t handle,
    uint32_t options,
    void* bytes,
    uint32_t num_bytes,
    uint32_t actual_bytes[1],
    mx_handle_t handles[],
    uint32_t num_handles,
    uint32_t actual_handles[1]) __attribute__((__leaf__));

extern mx_status_t _mx_channel_read(
    mx_handle_t handle,
    uint32_t options,
    void* bytes,
    uint32_t num_bytes,
    uint32_t actual_bytes[1],
    mx_handle_t handles[],
    uint32_t num_handles,
    uint32_t actual_handles[1]) __attribute__((__leaf__));

extern mx_status_t mx_channel_write(
    mx_handle_t handle,
    uint32_t options,
    const void* bytes,
    uint32_t num_bytes,
    const mx_handle_t handles[],
    uint32_t num_handles) __attribute__((__leaf__));

extern mx_status_t _mx_channel_write(
    mx_handle_t handle,
    uint32_t options,
    const void* bytes,
    uint32_t num_bytes,
    const mx_handle_t handles[],
    uint32_t num_handles) __attribute__((__leaf__));

extern mx_status_t mx_channel_call(
    mx_handle_t handle,
    uint32_t options,
    mx_time_t timeout,
    const mx_channel_call_args_t args[1],
    uint32_t actual_bytes[1],
    uint32_t actual_handles[1],
    mx_status_t read_status[1]) __attribute__((__leaf__));

extern mx_status_t _mx_channel_call(
    mx_handle_t handle,
    uint32_t options,
    mx_time_t timeout,
    const mx_channel_call_args_t args[1],
    uint32_t actual_bytes[1],
    uint32_t actual_handles[1],
    mx_status_t read_status[1]) __attribute__((__leaf__));

extern mx_status_t mx_socket_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]) __attribute__((__leaf__));

extern mx_status_t _mx_socket_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]) __attribute__((__leaf__));

extern mx_status_t mx_socket_write(
    mx_handle_t handle,
    uint32_t options,
    const void* buffer,
    size_t size,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t _mx_socket_write(
    mx_handle_t handle,
    uint32_t options,
    const void* buffer,
    size_t size,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t mx_socket_read(
    mx_handle_t handle,
    uint32_t options,
    void* buffer,
    size_t size,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t _mx_socket_read(
    mx_handle_t handle,
    uint32_t options,
    void* buffer,
    size_t size,
    size_t actual[1]) __attribute__((__leaf__));

extern void mx_thread_exit(void) __attribute__((__leaf__)) __attribute__((__noreturn__));

extern void _mx_thread_exit(void) __attribute__((__leaf__)) __attribute__((__noreturn__));

extern mx_status_t mx_thread_create(
    mx_handle_t process,
    const char name[],
    uint32_t name_len,
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_thread_create(
    mx_handle_t process,
    const char name[],
    uint32_t name_len,
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_thread_start(
    mx_handle_t handle,
    uintptr_t thread_entry,
    uintptr_t stack,
    uintptr_t arg1,
    uintptr_t arg2) __attribute__((__leaf__));

extern mx_status_t _mx_thread_start(
    mx_handle_t handle,
    uintptr_t thread_entry,
    uintptr_t stack,
    uintptr_t arg1,
    uintptr_t arg2) __attribute__((__leaf__));

extern mx_status_t mx_thread_read_state(
    mx_handle_t handle,
    uint32_t kind,
    void* buffer,
    uint32_t len,
    uint32_t actual[1]) __attribute__((__leaf__));

extern mx_status_t _mx_thread_read_state(
    mx_handle_t handle,
    uint32_t kind,
    void* buffer,
    uint32_t len,
    uint32_t actual[1]) __attribute__((__leaf__));

extern mx_status_t mx_thread_write_state(
    mx_handle_t handle,
    uint32_t kind,
    const void* buffer,
    uint32_t buffer_len) __attribute__((__leaf__));

extern mx_status_t _mx_thread_write_state(
    mx_handle_t handle,
    uint32_t kind,
    const void* buffer,
    uint32_t buffer_len) __attribute__((__leaf__));

extern void mx_process_exit(
    int retcode) __attribute__((__leaf__)) __attribute__((__noreturn__));

extern void _mx_process_exit(
    int retcode) __attribute__((__leaf__)) __attribute__((__noreturn__));

extern mx_status_t mx_process_create(
    mx_handle_t job,
    const char name[],
    uint32_t name_len,
    uint32_t options,
    mx_handle_t proc_handle[1],
    mx_handle_t vmar_handle[1]) __attribute__((__leaf__));

extern mx_status_t _mx_process_create(
    mx_handle_t job,
    const char name[],
    uint32_t name_len,
    uint32_t options,
    mx_handle_t proc_handle[1],
    mx_handle_t vmar_handle[1]) __attribute__((__leaf__));

extern mx_status_t mx_process_start(
    mx_handle_t process_handle,
    mx_handle_t thread_handle,
    uintptr_t entry,
    uintptr_t stack,
    mx_handle_t arg_handle,
    uintptr_t arg2) __attribute__((__leaf__));

extern mx_status_t _mx_process_start(
    mx_handle_t process_handle,
    mx_handle_t thread_handle,
    uintptr_t entry,
    uintptr_t stack,
    mx_handle_t arg_handle,
    uintptr_t arg2) __attribute__((__leaf__));

extern mx_status_t mx_process_read_memory(
    mx_handle_t proc,
    uintptr_t vaddr,
    void* buffer,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t _mx_process_read_memory(
    mx_handle_t proc,
    uintptr_t vaddr,
    void* buffer,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t mx_process_write_memory(
    mx_handle_t proc,
    uintptr_t vaddr,
    const void* buffer,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t _mx_process_write_memory(
    mx_handle_t proc,
    uintptr_t vaddr,
    const void* buffer,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t mx_job_create(
    mx_handle_t parent_job,
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_job_create(
    mx_handle_t parent_job,
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_task_resume(
    mx_handle_t task_handle,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t _mx_task_resume(
    mx_handle_t task_handle,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t mx_task_kill(
    mx_handle_t task_handle) __attribute__((__leaf__));

extern mx_status_t _mx_task_kill(
    mx_handle_t task_handle) __attribute__((__leaf__));

extern mx_status_t mx_event_create(
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_event_create(
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_eventpair_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]) __attribute__((__leaf__));

extern mx_status_t _mx_eventpair_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]) __attribute__((__leaf__));

extern mx_status_t mx_futex_wait(
    mx_futex_t value_ptr[1],
    int current_value,
    mx_time_t timeout) __attribute__((__leaf__));

extern mx_status_t _mx_futex_wait(
    mx_futex_t value_ptr[1],
    int current_value,
    mx_time_t timeout) __attribute__((__leaf__));

extern mx_status_t mx_futex_wake(
    mx_futex_t value_ptr[1],
    uint32_t count) __attribute__((__leaf__));

extern mx_status_t _mx_futex_wake(
    mx_futex_t value_ptr[1],
    uint32_t count) __attribute__((__leaf__));

extern mx_status_t mx_futex_requeue(
    mx_futex_t wake_ptr[1],
    uint32_t wake_count,
    int current_value,
    mx_futex_t requeue_ptr[1],
    uint32_t requeue_count) __attribute__((__leaf__));

extern mx_status_t _mx_futex_requeue(
    mx_futex_t wake_ptr[1],
    uint32_t wake_count,
    int current_value,
    mx_futex_t requeue_ptr[1],
    uint32_t requeue_count) __attribute__((__leaf__));

extern mx_status_t mx_waitset_create(
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_waitset_create(
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_waitset_add(
    mx_handle_t waitset_handle,
    uint64_t cookie,
    mx_handle_t handle,
    mx_signals_t signals) __attribute__((__leaf__));

extern mx_status_t _mx_waitset_add(
    mx_handle_t waitset_handle,
    uint64_t cookie,
    mx_handle_t handle,
    mx_signals_t signals) __attribute__((__leaf__));

extern mx_status_t mx_waitset_remove(
    mx_handle_t waitset_handle,
    uint64_t cookie) __attribute__((__leaf__));

extern mx_status_t _mx_waitset_remove(
    mx_handle_t waitset_handle,
    uint64_t cookie) __attribute__((__leaf__));

extern mx_status_t mx_waitset_wait(
    mx_handle_t waitset_handle,
    mx_time_t timeout,
    mx_waitset_result_t results[],
    uint32_t count[1]) __attribute__((__leaf__));

extern mx_status_t _mx_waitset_wait(
    mx_handle_t waitset_handle,
    mx_time_t timeout,
    mx_waitset_result_t results[],
    uint32_t count[1]) __attribute__((__leaf__));

extern mx_status_t mx_port_create(
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_port_create(
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_port_queue(
    mx_handle_t handle,
    const void* packet,
    size_t size) __attribute__((__leaf__));

extern mx_status_t _mx_port_queue(
    mx_handle_t handle,
    const void* packet,
    size_t size) __attribute__((__leaf__));

extern mx_status_t mx_port_wait(
    mx_handle_t handle,
    mx_time_t timeout,
    void* packet,
    size_t size) __attribute__((__leaf__));

extern mx_status_t _mx_port_wait(
    mx_handle_t handle,
    mx_time_t timeout,
    void* packet,
    size_t size) __attribute__((__leaf__));

extern mx_status_t mx_port_bind(
    mx_handle_t handle,
    uint64_t key,
    mx_handle_t source,
    mx_signals_t signals) __attribute__((__leaf__));

extern mx_status_t _mx_port_bind(
    mx_handle_t handle,
    uint64_t key,
    mx_handle_t source,
    mx_signals_t signals) __attribute__((__leaf__));

extern mx_status_t mx_vmo_create(
    uint64_t size,
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_vmo_create(
    uint64_t size,
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_vmo_read(
    mx_handle_t handle,
    void* data,
    uint64_t offset,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t _mx_vmo_read(
    mx_handle_t handle,
    void* data,
    uint64_t offset,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t mx_vmo_write(
    mx_handle_t handle,
    const void* data,
    uint64_t offset,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t _mx_vmo_write(
    mx_handle_t handle,
    const void* data,
    uint64_t offset,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t mx_vmo_get_size(
    mx_handle_t handle,
    uint64_t size[1]) __attribute__((__leaf__));

extern mx_status_t _mx_vmo_get_size(
    mx_handle_t handle,
    uint64_t size[1]) __attribute__((__leaf__));

extern mx_status_t mx_vmo_set_size(
    mx_handle_t handle,
    uint64_t size) __attribute__((__leaf__));

extern mx_status_t _mx_vmo_set_size(
    mx_handle_t handle,
    uint64_t size) __attribute__((__leaf__));

extern mx_status_t mx_vmo_op_range(
    mx_handle_t handle,
    uint32_t op,
    uint64_t offset,
    uint64_t size,
    void* buffer,
    size_t buffer_size) __attribute__((__leaf__));

extern mx_status_t _mx_vmo_op_range(
    mx_handle_t handle,
    uint32_t op,
    uint64_t offset,
    uint64_t size,
    void* buffer,
    size_t buffer_size) __attribute__((__leaf__));

extern mx_status_t mx_cprng_draw(
    void* buffer,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t _mx_cprng_draw(
    void* buffer,
    size_t len,
    size_t actual[1]) __attribute__((__leaf__));

extern mx_status_t mx_cprng_add_entropy(
    const void* buffer,
    size_t len) __attribute__((__leaf__));

extern mx_status_t _mx_cprng_add_entropy(
    const void* buffer,
    size_t len) __attribute__((__leaf__));

extern mx_status_t mx_fifo_create(
    uint32_t elem_count,
    uint32_t elem_size,
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]) __attribute__((__leaf__));

extern mx_status_t _mx_fifo_create(
    uint32_t elem_count,
    uint32_t elem_size,
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]) __attribute__((__leaf__));

extern mx_status_t mx_fifo_read(
    mx_handle_t handle,
    void* data,
    size_t len,
    uint32_t num_written[1]) __attribute__((__leaf__));

extern mx_status_t _mx_fifo_read(
    mx_handle_t handle,
    void* data,
    size_t len,
    uint32_t num_written[1]) __attribute__((__leaf__));

extern mx_status_t mx_fifo_write(
    mx_handle_t handle,
    const void* data,
    size_t len,
    uint32_t num_written[1]) __attribute__((__leaf__));

extern mx_status_t _mx_fifo_write(
    mx_handle_t handle,
    const void* data,
    size_t len,
    uint32_t num_written[1]) __attribute__((__leaf__));

extern mx_status_t mx_log_create(
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_log_create(
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_log_write(
    mx_handle_t handle,
    uint32_t len,
    const void* buffer,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t _mx_log_write(
    mx_handle_t handle,
    uint32_t len,
    const void* buffer,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t mx_log_read(
    mx_handle_t handle,
    uint32_t len,
    void* buffer,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t _mx_log_read(
    mx_handle_t handle,
    uint32_t len,
    void* buffer,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t mx_ktrace_read(
    mx_handle_t handle,
    void* data,
    uint32_t offset,
    uint32_t len,
    uint32_t actual[1]) __attribute__((__leaf__));

extern mx_status_t _mx_ktrace_read(
    mx_handle_t handle,
    void* data,
    uint32_t offset,
    uint32_t len,
    uint32_t actual[1]) __attribute__((__leaf__));

extern mx_status_t mx_ktrace_control(
    mx_handle_t handle,
    uint32_t action,
    uint32_t options,
    void* ptr) __attribute__((__leaf__));

extern mx_status_t _mx_ktrace_control(
    mx_handle_t handle,
    uint32_t action,
    uint32_t options,
    void* ptr) __attribute__((__leaf__));

extern mx_status_t mx_ktrace_write(
    mx_handle_t handle,
    uint32_t id,
    uint32_t arg0,
    uint32_t arg1) __attribute__((__leaf__));

extern mx_status_t _mx_ktrace_write(
    mx_handle_t handle,
    uint32_t id,
    uint32_t arg0,
    uint32_t arg1) __attribute__((__leaf__));

extern mx_status_t mx_mtrace_control(
    mx_handle_t handle,
    uint32_t kind,
    uint32_t action,
    uint32_t options,
    void* ptr,
    uint32_t size) __attribute__((__leaf__));

extern mx_status_t _mx_mtrace_control(
    mx_handle_t handle,
    uint32_t kind,
    uint32_t action,
    uint32_t options,
    void* ptr,
    uint32_t size) __attribute__((__leaf__));

extern mx_handle_t mx_debug_transfer_handle(
    mx_handle_t proc,
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_handle_t _mx_debug_transfer_handle(
    mx_handle_t proc,
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t mx_debug_read(
    mx_handle_t handle,
    void* buffer,
    uint32_t length) __attribute__((__leaf__));

extern mx_status_t _mx_debug_read(
    mx_handle_t handle,
    void* buffer,
    uint32_t length) __attribute__((__leaf__));

extern mx_status_t mx_debug_write(
    const void* buffer,
    uint32_t length) __attribute__((__leaf__));

extern mx_status_t _mx_debug_write(
    const void* buffer,
    uint32_t length) __attribute__((__leaf__));

extern mx_status_t mx_debug_send_command(
    mx_handle_t resource_handle,
    const void* buffer,
    uint32_t length) __attribute__((__leaf__));

extern mx_status_t _mx_debug_send_command(
    mx_handle_t resource_handle,
    const void* buffer,
    uint32_t length) __attribute__((__leaf__));

extern mx_handle_t mx_interrupt_create(
    mx_handle_t handle,
    uint32_t vector,
    uint32_t options) __attribute__((__leaf__));

extern mx_handle_t _mx_interrupt_create(
    mx_handle_t handle,
    uint32_t vector,
    uint32_t options) __attribute__((__leaf__));

extern mx_status_t mx_interrupt_complete(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t _mx_interrupt_complete(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t mx_interrupt_wait(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t _mx_interrupt_wait(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t mx_interrupt_signal(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t _mx_interrupt_signal(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t mx_mmap_device_io(
    mx_handle_t handle,
    uint32_t io_addr,
    uint32_t len) __attribute__((__leaf__));

extern mx_status_t _mx_mmap_device_io(
    mx_handle_t handle,
    uint32_t io_addr,
    uint32_t len) __attribute__((__leaf__));

extern mx_status_t mx_mmap_device_memory(
    mx_handle_t handle,
    mx_paddr_t paddr,
    uint32_t len,
    mx_cache_policy_t cache_policy,
    uintptr_t out_vaddr[1]) __attribute__((__leaf__));

extern mx_status_t _mx_mmap_device_memory(
    mx_handle_t handle,
    mx_paddr_t paddr,
    uint32_t len,
    mx_cache_policy_t cache_policy,
    uintptr_t out_vaddr[1]) __attribute__((__leaf__));

extern mx_status_t mx_io_mapping_get_info(
    mx_handle_t handle,
    uintptr_t out_vaddr[1],
    uint64_t out_size[1]) __attribute__((__leaf__));

extern mx_status_t _mx_io_mapping_get_info(
    mx_handle_t handle,
    uintptr_t out_vaddr[1],
    uint64_t out_size[1]) __attribute__((__leaf__));

extern mx_status_t mx_vmo_create_contiguous(
    mx_handle_t rsrc_handle,
    size_t size,
    uint32_t alignment_log2,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_vmo_create_contiguous(
    mx_handle_t rsrc_handle,
    size_t size,
    uint32_t alignment_log2,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_vmar_allocate(
    mx_handle_t parent_vmar_handle,
    size_t offset,
    size_t size,
    uint32_t flags,
    mx_handle_t child_vmar[1],
    uintptr_t child_addr[1]) __attribute__((__leaf__));

extern mx_status_t _mx_vmar_allocate(
    mx_handle_t parent_vmar_handle,
    size_t offset,
    size_t size,
    uint32_t flags,
    mx_handle_t child_vmar[1],
    uintptr_t child_addr[1]) __attribute__((__leaf__));

extern mx_status_t mx_vmar_destroy(
    mx_handle_t vmar_handle) __attribute__((__leaf__));

extern mx_status_t _mx_vmar_destroy(
    mx_handle_t vmar_handle) __attribute__((__leaf__));

extern mx_status_t mx_vmar_map(
    mx_handle_t vmar_handle,
    size_t vmar_offset,
    mx_handle_t vmo_handle,
    uint64_t vmo_offset,
    size_t len,
    uint32_t flags,
    uintptr_t mapped_addr[1]) __attribute__((__leaf__));

extern mx_status_t _mx_vmar_map(
    mx_handle_t vmar_handle,
    size_t vmar_offset,
    mx_handle_t vmo_handle,
    uint64_t vmo_offset,
    size_t len,
    uint32_t flags,
    uintptr_t mapped_addr[1]) __attribute__((__leaf__));

extern mx_status_t mx_vmar_unmap(
    mx_handle_t vmar_handle,
    uintptr_t addr,
    size_t len) __attribute__((__leaf__));

extern mx_status_t _mx_vmar_unmap(
    mx_handle_t vmar_handle,
    uintptr_t addr,
    size_t len) __attribute__((__leaf__));

extern mx_status_t mx_vmar_protect(
    mx_handle_t vmar_handle,
    uintptr_t addr,
    size_t len,
    uint32_t prot) __attribute__((__leaf__));

extern mx_status_t _mx_vmar_protect(
    mx_handle_t vmar_handle,
    uintptr_t addr,
    size_t len,
    uint32_t prot) __attribute__((__leaf__));

extern mx_status_t mx_bootloader_fb_get_info(
    uint32_t format[1],
    uint32_t width[1],
    uint32_t height[1],
    uint32_t stride[1]) __attribute__((__leaf__));

extern mx_status_t _mx_bootloader_fb_get_info(
    uint32_t format[1],
    uint32_t width[1],
    uint32_t height[1],
    uint32_t stride[1]) __attribute__((__leaf__));

extern mx_status_t mx_set_framebuffer(
    mx_handle_t handle,
    void* vaddr,
    uint32_t len,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t stride) __attribute__((__leaf__));

extern mx_status_t _mx_set_framebuffer(
    mx_handle_t handle,
    void* vaddr,
    uint32_t len,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t stride) __attribute__((__leaf__));

extern mx_status_t mx_clock_adjust(
    mx_handle_t handle,
    uint32_t clock_id,
    int64_t offset) __attribute__((__leaf__));

extern mx_status_t _mx_clock_adjust(
    mx_handle_t handle,
    uint32_t clock_id,
    int64_t offset) __attribute__((__leaf__));

extern mx_handle_t mx_pci_get_nth_device(
    mx_handle_t handle,
    uint32_t index,
    mx_pcie_get_nth_info_t out_info[1]) __attribute__((__leaf__));

extern mx_handle_t _mx_pci_get_nth_device(
    mx_handle_t handle,
    uint32_t index,
    mx_pcie_get_nth_info_t out_info[1]) __attribute__((__leaf__));

extern mx_status_t mx_pci_claim_device(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t _mx_pci_claim_device(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t mx_pci_enable_bus_master(
    mx_handle_t handle,
    bool enable) __attribute__((__leaf__));

extern mx_status_t _mx_pci_enable_bus_master(
    mx_handle_t handle,
    bool enable) __attribute__((__leaf__));

extern mx_status_t mx_pci_enable_pio(
    mx_handle_t handle,
    bool enable) __attribute__((__leaf__));

extern mx_status_t _mx_pci_enable_pio(
    mx_handle_t handle,
    bool enable) __attribute__((__leaf__));

extern mx_status_t mx_pci_reset_device(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t _mx_pci_reset_device(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_handle_t mx_pci_map_mmio(
    mx_handle_t handle,
    uint32_t bar_num,
    mx_cache_policy_t cache_policy) __attribute__((__leaf__));

extern mx_handle_t _mx_pci_map_mmio(
    mx_handle_t handle,
    uint32_t bar_num,
    mx_cache_policy_t cache_policy) __attribute__((__leaf__));

extern mx_status_t mx_pci_io_write(
    mx_handle_t handle,
    uint32_t bar_num,
    uint32_t offset,
    uint32_t len,
    uint32_t value) __attribute__((__leaf__));

extern mx_status_t _mx_pci_io_write(
    mx_handle_t handle,
    uint32_t bar_num,
    uint32_t offset,
    uint32_t len,
    uint32_t value) __attribute__((__leaf__));

extern mx_status_t mx_pci_io_read(
    mx_handle_t handle,
    uint32_t bar_num,
    uint32_t offset,
    uint32_t len,
    uint32_t out_value[1]) __attribute__((__leaf__));

extern mx_status_t _mx_pci_io_read(
    mx_handle_t handle,
    uint32_t bar_num,
    uint32_t offset,
    uint32_t len,
    uint32_t out_value[1]) __attribute__((__leaf__));

extern mx_handle_t mx_pci_map_interrupt(
    mx_handle_t handle,
    int32_t which_irq) __attribute__((__leaf__));

extern mx_handle_t _mx_pci_map_interrupt(
    mx_handle_t handle,
    int32_t which_irq) __attribute__((__leaf__));

extern mx_handle_t mx_pci_map_config(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_handle_t _mx_pci_map_config(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t mx_pci_query_irq_mode_caps(
    mx_handle_t handle,
    uint32_t mode,
    uint32_t out_max_irqs[1]) __attribute__((__leaf__));

extern mx_status_t _mx_pci_query_irq_mode_caps(
    mx_handle_t handle,
    uint32_t mode,
    uint32_t out_max_irqs[1]) __attribute__((__leaf__));

extern mx_status_t mx_pci_set_irq_mode(
    mx_handle_t handle,
    uint32_t mode,
    uint32_t requested_irq_count) __attribute__((__leaf__));

extern mx_status_t _mx_pci_set_irq_mode(
    mx_handle_t handle,
    uint32_t mode,
    uint32_t requested_irq_count) __attribute__((__leaf__));

extern mx_status_t mx_pci_init(
    mx_handle_t handle,
    const mx_pci_init_arg_t init_buf[],
    uint32_t len) __attribute__((__leaf__));

extern mx_status_t _mx_pci_init(
    mx_handle_t handle,
    const mx_pci_init_arg_t init_buf[],
    uint32_t len) __attribute__((__leaf__));

extern mx_status_t mx_pci_add_subtract_io_range(
    mx_handle_t handle,
    bool mmio,
    uint64_t base,
    uint64_t len,
    bool add) __attribute__((__leaf__));

extern mx_status_t _mx_pci_add_subtract_io_range(
    mx_handle_t handle,
    bool mmio,
    uint64_t base,
    uint64_t len,
    bool add) __attribute__((__leaf__));

extern uint32_t mx_acpi_uefi_rsdp(
    mx_handle_t handle) __attribute__((__leaf__));

extern uint32_t _mx_acpi_uefi_rsdp(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t mx_acpi_cache_flush(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t _mx_acpi_cache_flush(
    mx_handle_t handle) __attribute__((__leaf__));

extern mx_status_t mx_resource_create(
    mx_handle_t parent_handle,
    const mx_rrec_t records[],
    uint32_t count,
    mx_handle_t resource_out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_resource_create(
    mx_handle_t parent_handle,
    const mx_rrec_t records[],
    uint32_t count,
    mx_handle_t resource_out[1]) __attribute__((__leaf__));

extern mx_status_t mx_resource_get_handle(
    mx_handle_t handle,
    uint32_t index,
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t _mx_resource_get_handle(
    mx_handle_t handle,
    uint32_t index,
    uint32_t options,
    mx_handle_t out[1]) __attribute__((__leaf__));

extern mx_status_t mx_resource_do_action(
    mx_handle_t handle,
    uint32_t index,
    uint32_t action,
    uint32_t arg0,
    uint32_t arg1) __attribute__((__leaf__));

extern mx_status_t _mx_resource_do_action(
    mx_handle_t handle,
    uint32_t index,
    uint32_t action,
    uint32_t arg0,
    uint32_t arg1) __attribute__((__leaf__));

extern mx_status_t mx_resource_connect(
    mx_handle_t handle,
    mx_handle_t channel) __attribute__((__leaf__));

extern mx_status_t _mx_resource_connect(
    mx_handle_t handle,
    mx_handle_t channel) __attribute__((__leaf__));

extern mx_status_t mx_resource_accept(
    mx_handle_t handle,
    mx_handle_t channel[1]) __attribute__((__leaf__));

extern mx_status_t _mx_resource_accept(
    mx_handle_t handle,
    mx_handle_t channel[1]) __attribute__((__leaf__));

extern int mx_syscall_test_0(void) __attribute__((__leaf__));

extern int _mx_syscall_test_0(void) __attribute__((__leaf__));

extern int mx_syscall_test_1(
    int a) __attribute__((__leaf__));

extern int _mx_syscall_test_1(
    int a) __attribute__((__leaf__));

extern int mx_syscall_test_2(
    int a,
    int b) __attribute__((__leaf__));

extern int _mx_syscall_test_2(
    int a,
    int b) __attribute__((__leaf__));

extern int mx_syscall_test_3(
    int a,
    int b,
    int c) __attribute__((__leaf__));

extern int _mx_syscall_test_3(
    int a,
    int b,
    int c) __attribute__((__leaf__));

extern int mx_syscall_test_4(
    int a,
    int b,
    int c,
    int d) __attribute__((__leaf__));

extern int _mx_syscall_test_4(
    int a,
    int b,
    int c,
    int d) __attribute__((__leaf__));

extern int mx_syscall_test_5(
    int a,
    int b,
    int c,
    int d,
    int e) __attribute__((__leaf__));

extern int _mx_syscall_test_5(
    int a,
    int b,
    int c,
    int d,
    int e) __attribute__((__leaf__));

extern int mx_syscall_test_6(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f) __attribute__((__leaf__));

extern int _mx_syscall_test_6(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f) __attribute__((__leaf__));

extern int mx_syscall_test_7(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g) __attribute__((__leaf__));

extern int _mx_syscall_test_7(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g) __attribute__((__leaf__));

extern int mx_syscall_test_8(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g,
    int h) __attribute__((__leaf__));

extern int _mx_syscall_test_8(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g,
    int h) __attribute__((__leaf__));


