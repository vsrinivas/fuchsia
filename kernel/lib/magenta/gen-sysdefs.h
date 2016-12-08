// Copyright 2016 The Fuchsia Authors. All rights reserved.
// This is a GENERATED file. The license governing this file can be found in the LICENSE file.

mx_time_t sys_time_get(
    uint32_t clock_id);

mx_status_t sys_nanosleep(
    mx_time_t nanoseconds);

uint32_t sys_num_cpus();

mx_status_t sys_version_get(
    char version[],
    uint32_t version_len);

mx_status_t sys_handle_close(
    mx_handle_t handle);

mx_status_t sys_handle_duplicate(
    mx_handle_t handle,
    mx_rights_t rights,
    mx_handle_t out[1]);

mx_status_t sys_handle_replace(
    mx_handle_t handle,
    mx_rights_t rights,
    mx_handle_t out[1]);

mx_status_t sys_handle_wait_one(
    mx_handle_t handle,
    mx_signals_t waitfor,
    mx_time_t timeout,
    mx_signals_t observed[1]);

mx_status_t sys_handle_wait_many(
    mx_wait_item_t items[],
    uint32_t count,
    mx_time_t timeout);

mx_status_t sys_object_signal(
    mx_handle_t handle,
    uint32_t clear_mask,
    uint32_t set_mask);

mx_status_t sys_object_signal_peer(
    mx_handle_t handle,
    uint32_t clear_mask,
    uint32_t set_mask);

mx_status_t sys_object_get_property(
    mx_handle_t handle,
    uint32_t property,
    void* value,
    mx_size_t size);

mx_status_t sys_object_set_property(
    mx_handle_t handle,
    uint32_t property,
    const void* value,
    mx_size_t size);

mx_status_t sys_object_get_info(
    mx_handle_t handle,
    uint32_t topic,
    void* buffer,
    mx_size_t buffer_size,
    mx_size_t actual_count[1],
    mx_size_t avail_count[1]);

mx_status_t sys_object_get_child(
    mx_handle_t handle,
    uint64_t koid,
    mx_rights_t rights,
    mx_handle_t out[1]);

mx_status_t sys_object_bind_exception_port(
    mx_handle_t object,
    mx_handle_t eport,
    uint64_t key,
    uint32_t options);

mx_status_t sys_channel_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]);

mx_status_t sys_channel_read(
    mx_handle_t handle,
    uint32_t options,
    void* bytes,
    uint32_t num_bytes,
    uint32_t actual_bytes[1],
    mx_handle_t handles[],
    uint32_t num_handles,
    uint32_t actual_handles[1]);

mx_status_t sys_channel_write(
    mx_handle_t handle,
    uint32_t options,
    const void* bytes,
    uint32_t num_bytes,
    const mx_handle_t handles[],
    uint32_t num_handles);

mx_status_t sys_socket_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]);

mx_status_t sys_socket_write(
    mx_handle_t handle,
    uint32_t options,
    const void* buffer,
    mx_size_t size,
    mx_size_t actual[1]);

mx_status_t sys_socket_read(
    mx_handle_t handle,
    uint32_t options,
    void* buffer,
    mx_size_t size,
    mx_size_t actual[1]);

void sys_thread_exit();

mx_status_t sys_thread_create(
    mx_handle_t process,
    const char name[],
    uint32_t name_len,
    uint32_t options,
    mx_handle_t out[1]);

mx_status_t sys_thread_start(
    mx_handle_t handle,
    uintptr_t thread_entry,
    uintptr_t stack,
    uintptr_t arg1,
    uintptr_t arg2);

mx_status_t sys_thread_read_state(
    mx_handle_t handle,
    uint32_t kind,
    void* buffer,
    uint32_t len,
    uint32_t actual[1]);

mx_status_t sys_thread_write_state(
    mx_handle_t handle,
    uint32_t kind,
    const void* buffer,
    uint32_t buffer_len);

void sys_process_exit(
    int retcode);

mx_status_t sys_process_create(
    mx_handle_t job,
    const char name[],
    uint32_t name_len,
    uint32_t options,
    mx_handle_t out[1]);

mx_status_t sys_process_start(
    mx_handle_t process_handle,
    mx_handle_t thread_handle,
    uintptr_t entry,
    uintptr_t stack,
    mx_handle_t arg_handle,
    uintptr_t arg2);

mx_status_t sys_process_map_vm(
    mx_handle_t proc_handle,
    mx_handle_t vmo_handle,
    uint64_t offset,
    mx_size_t len,
    uintptr_t ptr[1],
    uint32_t options);

mx_status_t sys_process_unmap_vm(
    mx_handle_t proc_handle,
    uintptr_t address,
    mx_size_t len);

mx_status_t sys_process_protect_vm(
    mx_handle_t proc_handle,
    uintptr_t address,
    mx_size_t len,
    uint32_t prot);

mx_status_t sys_process_read_memory(
    mx_handle_t proc,
    uintptr_t vaddr,
    void* buffer,
    mx_size_t len,
    mx_size_t actual[1]);

mx_status_t sys_process_write_memory(
    mx_handle_t proc,
    uintptr_t vaddr,
    const void* buffer,
    mx_size_t len,
    mx_size_t actual[1]);

mx_status_t sys_job_create(
    mx_handle_t parent_job,
    uint32_t options,
    mx_handle_t out[1]);

mx_status_t sys_task_resume(
    mx_handle_t task_handle,
    uint32_t options);

mx_status_t sys_task_kill(
    mx_handle_t task_handle);

mx_status_t sys_event_create(
    uint32_t options,
    mx_handle_t out[1]);

mx_status_t sys_eventpair_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]);

mx_status_t sys_futex_wait(
    mx_futex_t value_ptr[1],
    int current_value,
    mx_time_t timeout);

mx_status_t sys_futex_wake(
    mx_futex_t value_ptr[1],
    uint32_t count);

mx_status_t sys_futex_requeue(
    mx_futex_t wake_ptr[1],
    uint32_t wake_count,
    int current_value,
    mx_futex_t requeue_ptr[1],
    uint32_t requeue_count);

mx_status_t sys_waitset_create(
    uint32_t options,
    mx_handle_t out[1]);

mx_status_t sys_waitset_add(
    mx_handle_t waitset_handle,
    uint64_t cookie,
    mx_handle_t handle,
    mx_signals_t signals);

mx_status_t sys_waitset_remove(
    mx_handle_t waitset_handle,
    uint64_t cookie);

mx_status_t sys_waitset_wait(
    mx_handle_t waitset_handle,
    mx_time_t timeout,
    mx_waitset_result_t results[],
    uint32_t count[1]);

mx_status_t sys_port_create(
    uint32_t options,
    mx_handle_t out[1]);

mx_status_t sys_port_queue(
    mx_handle_t handle,
    const void* packet,
    mx_size_t size);

mx_status_t sys_port_wait(
    mx_handle_t handle,
    mx_time_t timeout,
    void* packet,
    mx_size_t size);

mx_status_t sys_port_bind(
    mx_handle_t handle,
    uint64_t key,
    mx_handle_t source,
    mx_signals_t signals);

mx_status_t sys_vmo_create(
    uint64_t size,
    uint32_t options,
    mx_handle_t out[1]);

mx_status_t sys_vmo_read(
    mx_handle_t handle,
    void* data,
    uint64_t offset,
    mx_size_t len,
    mx_size_t actual);

mx_status_t sys_vmo_write(
    mx_handle_t handle,
    const void* data,
    uint64_t offset,
    mx_size_t len,
    mx_size_t actual);

mx_status_t sys_vmo_get_size(
    mx_handle_t handle,
    uint64_t size[1]);

mx_status_t sys_vmo_set_size(
    mx_handle_t handle,
    uint64_t size);

mx_status_t sys_vmo_op_range(
    mx_handle_t handle,
    uint32_t op,
    uint64_t offset,
    uint64_t size,
    void* buffer,
    mx_size_t buffer_size);

mx_status_t sys_cprng_draw(
    void* buffer,
    mx_size_t len,
    mx_size_t actual[1]);

mx_status_t sys_cprng_add_entropy(
    const void* buffer,
    mx_size_t len);

mx_handle_t sys_log_create(
    uint32_t options);

mx_status_t sys_log_write(
    mx_handle_t handle,
    uint32_t len,
    const void* buffer,
    uint32_t options);

mx_status_t sys_log_read(
    mx_handle_t handle,
    uint32_t len,
    void* buffer,
    uint32_t options);

mx_status_t sys_ktrace_read(
    mx_handle_t handle,
    void* data,
    uint32_t offset,
    uint32_t len,
    uint32_t actual[1]);

mx_status_t sys_ktrace_control(
    mx_handle_t handle,
    uint32_t action,
    uint32_t options,
    void* ptr);

mx_status_t sys_ktrace_write(
    mx_handle_t handle,
    uint32_t id,
    uint32_t arg0,
    uint32_t arg1);

mx_status_t sys_thread_arch_prctl(
    mx_handle_t handle,
    uint32_t op,
    uintptr_t value_ptr[1]);

mx_handle_t sys_debug_transfer_handle(
    mx_handle_t proc,
    mx_handle_t handle);

mx_status_t sys_debug_read(
    mx_handle_t handle,
    void* buffer,
    uint32_t length);

mx_status_t sys_debug_write(
    const void* buffer,
    uint32_t length);

mx_status_t sys_debug_send_command(
    mx_handle_t resource_handle,
    const void* buffer,
    uint32_t length);

mx_handle_t sys_interrupt_create(
    mx_handle_t handle,
    uint32_t vector,
    uint32_t options);

mx_status_t sys_interrupt_complete(
    mx_handle_t handle);

mx_status_t sys_interrupt_wait(
    mx_handle_t handle);

mx_status_t sys_mmap_device_io(
    mx_handle_t handle,
    uint32_t io_addr,
    uint32_t len);

mx_status_t sys_mmap_device_memory(
    mx_handle_t handle,
    mx_paddr_t paddr,
    uint32_t len,
    mx_cache_policy_t cache_policy,
    void* out_vaddr);

mx_status_t sys_io_mapping_get_info(
    mx_handle_t handle,
    uintptr_t out_vaddr[1],
    uint64_t out_size[1]);

mx_status_t sys_vmo_create_contiguous(
    mx_handle_t rsrc_handle,
    mx_size_t size,
    mx_handle_t out[1]);

mx_status_t sys_bootloader_fb_get_info(
    uint32_t format[1],
    uint32_t width[1],
    uint32_t height[1],
    uint32_t stride[1]);

mx_status_t sys_set_framebuffer(
    mx_handle_t handle,
    void* vaddr,
    uint32_t len,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t stride);

mx_status_t sys_clock_adjust(
    mx_handle_t handle,
    uint32_t clock_id,
    int64_t offset);

mx_handle_t sys_pci_get_nth_device(
    mx_handle_t handle,
    uint32_t index,
    mx_pcie_get_nth_info_t out_info[1]);

mx_status_t sys_pci_claim_device(
    mx_handle_t handle);

mx_status_t sys_pci_enable_bus_master(
    mx_handle_t handle,
    bool enable);

mx_status_t sys_pci_reset_device(
    mx_handle_t handle);

mx_handle_t sys_pci_map_mmio(
    mx_handle_t handle,
    uint32_t bar_num,
    mx_cache_policy_t cache_policy);

mx_status_t sys_pci_io_write(
    mx_handle_t handle,
    uint32_t bar_num,
    uint32_t offset,
    uint32_t len,
    uint32_t value);

mx_status_t sys_pci_io_read(
    mx_handle_t handle,
    uint32_t bar_num,
    uint32_t offset,
    uint32_t len,
    uint32_t out_value[1]);

mx_handle_t sys_pci_map_interrupt(
    mx_handle_t handle,
    int32_t which_irq);

mx_handle_t sys_pci_map_config(
    mx_handle_t handle);

mx_status_t sys_pci_query_irq_mode_caps(
    mx_handle_t handle,
    uint32_t mode,
    uint32_t out_max_irqs[1]);

mx_status_t sys_pci_set_irq_mode(
    mx_handle_t handle,
    uint32_t mode,
    uint32_t requested_irq_count);

mx_status_t sys_pci_init(
    mx_handle_t handle,
    const mx_pci_init_arg_t init_buf[],
    uint32_t len);

mx_status_t sys_mx_pci_add_subtract_io_range(
    mx_handle_t handle,
    bool mmio,
    uint64_t base,
    uint64_t len,
    bool add);

uint32_t sys_acpi_uefi_rsdp(
    mx_handle_t handle);

mx_status_t sys_acpi_cache_flush(
    mx_handle_t handle);

mx_status_t sys_resource_create(
    mx_handle_t parent_handle,
    const mx_rrec_t records[],
    uint32_t count,
    mx_handle_t resource_out[1]);

mx_status_t sys_resource_get_handle(
    mx_handle_t handle,
    uint32_t index,
    uint32_t options,
    mx_handle_t out[1]);

mx_status_t sys_resource_do_action(
    mx_handle_t handle,
    uint32_t index,
    uint32_t action,
    uint32_t arg0,
    uint32_t arg1);

mx_status_t sys_resource_connect(
    mx_handle_t handle,
    mx_handle_t channel);

mx_status_t sys_resource_accept(
    mx_handle_t handle,
    mx_handle_t channel[1]);

int sys_syscall_test_0();

int sys_syscall_test_1(
    int a);

int sys_syscall_test_2(
    int a,
    int b);

int sys_syscall_test_3(
    int a,
    int b,
    int c);

int sys_syscall_test_4(
    int a,
    int b,
    int c,
    int d);

int sys_syscall_test_5(
    int a,
    int b,
    int c,
    int d,
    int e);

int sys_syscall_test_6(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f);

int sys_syscall_test_7(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g);

int sys_syscall_test_8(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g,
    int h);


