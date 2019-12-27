// Copyright 2017 The Fuchsia Authors. All rights reserved.
// The license governing this file can be found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
#[link(name = "zircon")]
extern "C" {
    pub fn zx_clock_get(clock_id: zx_clock_t, out: *mut zx_time_t) -> zx_status_t;

    pub fn zx_clock_get_monotonic() -> zx_time_t;

    pub fn zx_nanosleep(deadline: zx_time_t) -> zx_status_t;

    pub fn zx_ticks_get() -> zx_ticks_t;

    pub fn zx_ticks_per_second() -> zx_ticks_t;

    pub fn zx_deadline_after(nanoseconds: zx_duration_t) -> zx_time_t;

    pub fn zx_clock_adjust(handle: zx_handle_t, clock_id: zx_clock_t, offset: i64) -> zx_status_t;

    pub fn zx_system_get_dcache_line_size() -> u32;

    pub fn zx_system_get_num_cpus() -> u32;

    pub fn zx_system_get_version(version: *mut u8, version_size: usize) -> zx_status_t;

    pub fn zx_system_get_physmem() -> u64;

    pub fn zx_system_get_features(kind: u32, features: *mut u32) -> zx_status_t;

    pub fn zx_cache_flush(addr: *const u8, size: usize, options: u32) -> zx_status_t;

    pub fn zx_handle_close(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_handle_close_many(handles: *const zx_handle_t, num_handles: usize) -> zx_status_t;

    pub fn zx_handle_duplicate(
        handle: zx_handle_t,
        rights: zx_rights_t,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_handle_replace(
        handle: zx_handle_t,
        rights: zx_rights_t,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_object_wait_one(
        handle: zx_handle_t,
        signals: zx_signals_t,
        deadline: zx_time_t,
        observed: *mut zx_signals_t,
    ) -> zx_status_t;

    pub fn zx_object_wait_many(
        items: *mut zx_wait_item_t,
        count: usize,
        deadline: zx_time_t,
    ) -> zx_status_t;

    pub fn zx_object_wait_async(
        handle: zx_handle_t,
        port: zx_handle_t,
        key: u64,
        signals: zx_signals_t,
        options: u32,
    ) -> zx_status_t;

    pub fn zx_object_signal(handle: zx_handle_t, clear_mask: u32, set_mask: u32) -> zx_status_t;

    pub fn zx_object_signal_peer(
        handle: zx_handle_t,
        clear_mask: u32,
        set_mask: u32,
    ) -> zx_status_t;

    pub fn zx_object_get_property(
        handle: zx_handle_t,
        property: u32,
        value: *mut u8,
        value_size: usize,
    ) -> zx_status_t;

    pub fn zx_object_set_property(
        handle: zx_handle_t,
        property: u32,
        value: *const u8,
        value_size: usize,
    ) -> zx_status_t;

    pub fn zx_object_get_info(
        handle: zx_handle_t,
        topic: u32,
        buffer: *mut u8,
        buffer_size: usize,
        actual: *mut usize,
        avail: *mut usize,
    ) -> zx_status_t;

    pub fn zx_object_get_child(
        handle: zx_handle_t,
        koid: u64,
        rights: zx_rights_t,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_channel_create(
        options: u32,
        out0: *mut zx_handle_t,
        out1: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_channel_read(
        handle: zx_handle_t,
        options: u32,
        bytes: *mut u8,
        handles: *mut zx_handle_t,
        num_bytes: u32,
        num_handles: u32,
        actual_bytes: *mut u32,
        actual_handles: *mut u32,
    ) -> zx_status_t;

    pub fn zx_channel_write(
        handle: zx_handle_t,
        options: u32,
        bytes: *const u8,
        num_bytes: u32,
        handles: *const zx_handle_t,
        num_handles: u32,
    ) -> zx_status_t;

    pub fn zx_channel_write_etc(
        handle: zx_handle_t,
        options: u32,
        bytes: *const u8,
        num_bytes: u32,
        handles: *mut zx_handle_disposition_t,
        num_handles: u32,
    ) -> zx_status_t;

    pub fn zx_channel_call(
        handle: zx_handle_t,
        options: u32,
        deadline: zx_time_t,
        args: *const zx_channel_call_args_t,
        actual_bytes: *mut u32,
        actual_handles: *mut u32,
    ) -> zx_status_t;

    pub fn zx_socket_create(
        options: u32,
        out0: *mut zx_handle_t,
        out1: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_socket_write(
        handle: zx_handle_t,
        options: u32,
        buffer: *const u8,
        buffer_size: usize,
        actual: *mut usize,
    ) -> zx_status_t;

    pub fn zx_socket_read(
        handle: zx_handle_t,
        options: u32,
        buffer: *mut u8,
        buffer_size: usize,
        actual: *mut usize,
    ) -> zx_status_t;

    pub fn zx_socket_shutdown(handle: zx_handle_t, options: u32) -> zx_status_t;

    pub fn zx_thread_exit();

    pub fn zx_thread_create(
        process: zx_handle_t,
        name: *const u8,
        name_size: usize,
        options: u32,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_thread_start(
        handle: zx_handle_t,
        thread_entry: zx_vaddr_t,
        stack: zx_vaddr_t,
        arg1: usize,
        arg2: usize,
    ) -> zx_status_t;

    pub fn zx_thread_read_state(
        handle: zx_handle_t,
        kind: u32,
        buffer: *mut u8,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_thread_write_state(
        handle: zx_handle_t,
        kind: u32,
        buffer: *const u8,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_process_exit(retcode: i64) -> !;

    pub fn zx_process_create(
        job: zx_handle_t,
        name: *const u8,
        name_size: usize,
        options: u32,
        proc_handle: *mut zx_handle_t,
        vmar_handle: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_process_start(
        handle: zx_handle_t,
        thread: zx_handle_t,
        entry: zx_vaddr_t,
        stack: zx_vaddr_t,
        arg1: zx_handle_t,
        arg2: usize,
    ) -> zx_status_t;

    pub fn zx_process_read_memory(
        handle: zx_handle_t,
        vaddr: zx_vaddr_t,
        buffer: *mut u8,
        buffer_size: usize,
        actual: *mut usize,
    ) -> zx_status_t;

    pub fn zx_process_write_memory(
        handle: zx_handle_t,
        vaddr: zx_vaddr_t,
        buffer: *const u8,
        buffer_size: usize,
        actual: *mut usize,
    ) -> zx_status_t;

    pub fn zx_job_default() -> zx_handle_t;

    pub fn zx_job_create(
        parent_job: zx_handle_t,
        options: u32,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_job_set_policy(
        handle: zx_handle_t,
        options: u32,
        topic: u32,
        policy: *const u8,
        count: u32,
    ) -> zx_status_t;

    pub fn zx_task_bind_exception_port(
        handle: zx_handle_t,
        port: zx_handle_t,
        key: u64,
        options: u32,
    ) -> zx_status_t;

    pub fn zx_task_suspend(handle: zx_handle_t, token: *mut zx_handle_t) -> zx_status_t;

    pub fn zx_task_suspend_token(handle: zx_handle_t, token: *mut zx_handle_t) -> zx_status_t;

    pub fn zx_task_resume_from_exception(
        handle: zx_handle_t,
        port: zx_handle_t,
        options: u32,
    ) -> zx_status_t;

    pub fn zx_task_kill(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_event_create(options: u32, out: *mut zx_handle_t) -> zx_status_t;

    pub fn zx_eventpair_create(
        options: u32,
        out0: *mut zx_handle_t,
        out1: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_futex_wait(
        value_ptr: *const zx_futex_t,
        current_value: zx_futex_t,
        new_futex_owner: zx_handle_t,
        deadline: zx_time_t,
    ) -> zx_status_t;

    pub fn zx_futex_wake(value_ptr: *const zx_futex_t, wake_count: u32) -> zx_status_t;

    pub fn zx_futex_requeue(
        value_ptr: *const zx_futex_t,
        wake_count: u32,
        current_value: zx_futex_t,
        requeue_ptr: *const zx_futex_t,
        requeue_count: u32,
        new_requeue_owner: zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_futex_wake_single_owner(value_ptr: *const zx_futex_t) -> zx_status_t;

    pub fn zx_futex_requeue_single_owner(
        value_ptr: *const zx_futex_t,
        current_value: zx_futex_t,
        requeue_ptr: *const zx_futex_t,
        requeue_count: u32,
        new_requeue_owner: zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_futex_get_owner(value_ptr: *const zx_futex_t, koid: *mut zx_koid_t) -> zx_status_t;

    pub fn zx_port_create(options: u32, out: *mut zx_handle_t) -> zx_status_t;

    pub fn zx_port_queue(handle: zx_handle_t, packet: *const zx_port_packet_t) -> zx_status_t;

    pub fn zx_port_wait(
        handle: zx_handle_t,
        deadline: zx_time_t,
        packet: *mut zx_port_packet_t,
    ) -> zx_status_t;

    pub fn zx_port_cancel(handle: zx_handle_t, source: zx_handle_t, key: u64) -> zx_status_t;

    pub fn zx_timer_create(
        options: u32,
        clock_id: zx_clock_t,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_timer_set(
        handle: zx_handle_t,
        deadline: zx_time_t,
        slack: zx_duration_t,
    ) -> zx_status_t;

    pub fn zx_timer_cancel(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_vmo_create(size: u64, options: u32, out: *mut zx_handle_t) -> zx_status_t;

    pub fn zx_vmo_read(
        handle: zx_handle_t,
        buffer: *mut u8,
        offset: u64,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_vmo_write(
        handle: zx_handle_t,
        buffer: *const u8,
        offset: u64,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_vmo_get_size(handle: zx_handle_t, size: *mut u64) -> zx_status_t;

    pub fn zx_vmo_set_size(handle: zx_handle_t, size: u64) -> zx_status_t;

    pub fn zx_vmo_op_range(
        handle: zx_handle_t,
        op: u32,
        offset: u64,
        size: u64,
        buffer: *mut u8,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_vmo_create_child(
        handle: zx_handle_t,
        options: u32,
        offset: u64,
        size: u64,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_vmo_set_cache_policy(handle: zx_handle_t, cache_policy: u32) -> zx_status_t;

    pub fn zx_vmo_replace_as_executable(
        handle: zx_handle_t,
        vmex: zx_handle_t,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_vmar_allocate(
        parent_vmar: zx_handle_t,
        options: zx_vm_option_t,
        offset: usize,
        size: usize,
        child_vmar: *mut zx_handle_t,
        child_addr: *mut usize,
    ) -> zx_status_t;

    pub fn zx_vmar_destroy(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_vmar_map(
        handle: zx_handle_t,
        options: zx_vm_option_t,
        vmar_offset: usize,
        vmo_handle: zx_handle_t,
        vmo_offset: u64,
        len: usize,
        mapped_addr: *mut usize,
    ) -> zx_status_t;

    pub fn zx_vmar_unmap(vmar_handle: zx_handle_t, addr: usize, len: usize) -> zx_status_t;

    pub fn zx_vmar_protect(
        handle: zx_handle_t,
        options: zx_vm_option_t,
        addr: usize,
        len: usize,
    ) -> zx_status_t;

    pub fn zx_vmar_root_self() -> zx_handle_t;

    pub fn zx_cprng_draw(buffer: *mut u8, buffer_size: usize);

    pub fn zx_cprng_add_entropy(buffer: *const u8, buffer_size: usize) -> zx_status_t;

    pub fn zx_fifo_create(
        elem_count: usize,
        elem_size: usize,
        options: u32,
        out0: *mut zx_handle_t,
        out1: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_fifo_read(
        handle: zx_handle_t,
        elem_size: usize,
        data: *mut u8,
        count: usize,
        actual_count: *mut usize,
    ) -> zx_status_t;

    pub fn zx_fifo_write(
        handle: zx_handle_t,
        elem_size: usize,
        data: *const u8,
        count: usize,
        actual_count: *mut usize,
    ) -> zx_status_t;

    pub fn zx_vmar_unmap_handle_close_thread_exit(
        vmar_handle: zx_handle_t,
        addr: usize,
        len: usize,
        handle: zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_futex_wake_handle_close_thread_exit(
        value_ptr: *const zx_futex_t,
        count: u32,
        new_value: isize,
        handle: zx_handle_t,
    );

    pub fn zx_debuglog_create(
        resource: zx_handle_t,
        options: u32,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_debuglog_write(
        handle: zx_handle_t,
        options: u32,
        buffer: *const u8,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_debuglog_read(
        handle: zx_handle_t,
        options: u32,
        buffer: *mut u8,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_ktrace_read(
        handle: zx_handle_t,
        data: *mut u8,
        offset: u32,
        data_size: usize,
        actual: *mut usize,
    ) -> zx_status_t;

    pub fn zx_ktrace_control(
        handle: zx_handle_t,
        action: u32,
        options: u32,
        ptr: *mut u8,
    ) -> zx_status_t;

    pub fn zx_ktrace_write(handle: zx_handle_t, id: u32, arg0: u32, arg1: u32) -> zx_status_t;

    pub fn zx_mtrace_control(
        handle: zx_handle_t,
        kind: u32,
        action: u32,
        options: u32,
        ptr: *mut u8,
        ptr_size: usize,
    ) -> zx_status_t;

    pub fn zx_debug_read(
        handle: zx_handle_t,
        buffer: *mut u8,
        buffer_size: usize,
        actual: *mut usize,
    ) -> zx_status_t;

    pub fn zx_debug_write(buffer: *const u8, buffer_size: usize) -> zx_status_t;

    pub fn zx_debug_send_command(
        resource: zx_handle_t,
        buffer: *const u8,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_interrupt_create(
        src_obj: zx_handle_t,
        src_num: u32,
        options: u32,
        out_handle: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_interrupt_bind(
        handle: zx_handle_t,
        port_handle: zx_handle_t,
        key: u64,
        options: u32,
    ) -> zx_status_t;

    pub fn zx_interrupt_wait(handle: zx_handle_t, out_timestamp: *mut zx_time_t) -> zx_status_t;

    pub fn zx_interrupt_destroy(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_interrupt_ack(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_interrupt_trigger(
        handle: zx_handle_t,
        options: u32,
        timestamp: zx_time_t,
    ) -> zx_status_t;

    pub fn zx_interrupt_bind_vcpu(
        handle: zx_handle_t,
        vcpu: zx_handle_t,
        options: u32,
    ) -> zx_status_t;

    pub fn zx_ioports_request(resource: zx_handle_t, io_addr: u16, len: u32) -> zx_status_t;

    pub fn zx_vmo_create_contiguous(
        bti: zx_handle_t,
        size: usize,
        alignment_log2: u32,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_vmo_create_physical(
        resource: zx_handle_t,
        paddr: zx_paddr_t,
        size: usize,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_iommu_create(
        resource: zx_handle_t,
        type_: u32,
        desc: *const u8,
        desc_size: usize,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_bti_create(
        iommu: zx_handle_t,
        options: u32,
        bti_id: u64,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_bti_pin(
        handle: zx_handle_t,
        options: u32,
        vmo: zx_handle_t,
        offset: u64,
        size: u64,
        addrs: *mut zx_paddr_t,
        addrs_count: usize,
        pmt: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_bti_release_quarantine(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_pmt_unpin(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_framebuffer_get_info(
        resource: zx_handle_t,
        format: *mut u32,
        width: *mut u32,
        height: *mut u32,
        stride: *mut u32,
    ) -> zx_status_t;

    pub fn zx_framebuffer_set_range(
        resource: zx_handle_t,
        vmo: zx_handle_t,
        len: u32,
        format: u32,
        width: u32,
        height: u32,
        stride: u32,
    ) -> zx_status_t;

    pub fn zx_pci_get_nth_device(
        handle: zx_handle_t,
        index: u32,
        out_info: *mut zx_pcie_device_info_t,
        out_handle: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_pci_enable_bus_master(handle: zx_handle_t, enable: bool) -> zx_status_t;

    pub fn zx_pci_enable_pio(handle: zx_handle_t, enable: bool) -> zx_status_t;

    pub fn zx_pci_reset_device(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_pci_cfg_pio_rw(
        handle: zx_handle_t,
        bus: u8,
        dev: u8,
        func: u8,
        offset: u8,
        val: *mut u32,
        width: usize,
        write: bool,
    ) -> zx_status_t;

    pub fn zx_pci_get_bar(
        handle: zx_handle_t,
        bar_num: u32,
        out_bar: *mut zx_pci_resource_t,
    ) -> zx_status_t;

    pub fn zx_pci_get_config(
        handle: zx_handle_t,
        out_config: *mut zx_pci_resource_t,
    ) -> zx_status_t;

    pub fn zx_pci_map_interrupt(
        handle: zx_handle_t,
        which_irq: i32,
        out_handle: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_pci_query_irq_mode_caps(
        handle: zx_handle_t,
        mode: u32,
        out_max_irqs: *mut u32,
    ) -> zx_status_t;

    pub fn zx_pci_set_irq_mode(
        handle: zx_handle_t,
        mode: u32,
        requested_irq_count: u32,
    ) -> zx_status_t;

    pub fn zx_pci_init(
        handle: zx_handle_t,
        init_buf: *const zx_pci_init_arg_t,
        len: u32, // actually is u32
    ) -> zx_status_t;

    pub fn zx_pci_add_subtract_io_range(
        handle: zx_handle_t,
        mmio: bool,
        base: u64,
        len: u64,
        add: bool,
    ) -> zx_status_t;

    pub fn zx_acpi_uefi_rsdp(handle: zx_handle_t) -> u64;

    pub fn zx_acpi_cache_flush(handle: zx_handle_t) -> zx_status_t;

    pub fn zx_resource_create(
        parent_rsrc: zx_handle_t,
        options: u32,
        base: u64,
        size: usize,
        name: *const u8,
        name_size: usize,
        resource_out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_guest_create(
        resource: zx_handle_t,
        options: u32,
        guest_handle: *mut zx_handle_t,
        vmar_handle: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_guest_set_trap(
        handle: zx_handle_t,
        kind: zx_guest_trap_t,
        addr: zx_vaddr_t,
        size: usize,
        port_handle: zx_handle_t,
        key: u64,
    ) -> zx_status_t;

    pub fn zx_vcpu_create(
        guest: zx_handle_t,
        options: u32,
        entry: zx_vaddr_t,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_vcpu_resume(handle: zx_handle_t, packet: *mut zx_port_packet_t) -> zx_status_t;

    pub fn zx_vcpu_interrupt(handle: zx_handle_t, vector: u32) -> zx_status_t;

    pub fn zx_vcpu_read_state(
        handle: zx_handle_t,
        kind: u32,
        buffer: *mut u8,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_vcpu_write_state(
        handle: zx_handle_t,
        kind: u32,
        buffer: *const u8,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_system_mexec(
        resource: zx_handle_t,
        kernel_vmo: zx_handle_t,
        bootimage_vmo: zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_system_mexec_payload_get(
        resource: zx_handle_t,
        buffer: *mut u8,
        buffer_size: usize,
    ) -> zx_status_t;

    pub fn zx_pager_create(options: u32, out: *mut zx_handle_t) -> zx_status_t;

    pub fn zx_pager_create_vmo(
        pager: zx_handle_t,
        options: u32,
        port: zx_handle_t,
        key: u64,
        size: u64,
        out: *mut zx_handle_t,
    ) -> zx_status_t;

    pub fn zx_pager_detach_vmo(pager: zx_handle_t, vmo: zx_handle_t) -> zx_status_t;

    pub fn zx_pager_supply_pages(
        pager: zx_handle_t,
        pager_vmo: zx_handle_t,
        offset: u64,
        length: u64,
        aux_vmo: zx_handle_t,
        aux_offset: u64,
    ) -> zx_status_t;

    pub fn zx_syscall_test_0() -> zx_status_t;

    pub fn zx_syscall_test_1(a: isize) -> zx_status_t;

    pub fn zx_syscall_test_2(a: isize, b: isize) -> zx_status_t;

    pub fn zx_syscall_test_3(a: isize, b: isize, c: isize) -> zx_status_t;

    pub fn zx_syscall_test_4(a: isize, b: isize, c: isize, d: isize) -> zx_status_t;

    pub fn zx_syscall_test_5(a: isize, b: isize, c: isize, d: isize, e: isize) -> zx_status_t;

    pub fn zx_syscall_test_6(
        a: isize,
        b: isize,
        c: isize,
        d: isize,
        e: isize,
        f: isize,
    ) -> zx_status_t;

    pub fn zx_syscall_test_7(
        a: isize,
        b: isize,
        c: isize,
        d: isize,
        e: isize,
        f: isize,
        g: isize,
    ) -> zx_status_t;

    pub fn zx_syscall_test_8(
        a: isize,
        b: isize,
        c: isize,
        d: isize,
        e: isize,
        f: isize,
        g: isize,
        h: isize,
    ) -> zx_status_t;

    pub fn zx_syscall_test_wrapper(a: isize, b: isize, c: isize) -> zx_status_t;
}
