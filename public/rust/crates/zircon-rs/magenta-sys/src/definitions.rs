// Copyright 2017 The Fuchsia Authors. All rights reserved.
// This is a GENERATED file, see //magenta/system/host/sysgen.
// The license governing this file can be found in the LICENSE file.

#[link(name = "magenta")]
extern {
    pub fn mx_time_get(
        clock_id: u32
        ) -> mx_time_t;

    pub fn mx_nanosleep(
        deadline: mx_time_t
        ) -> mx_status_t;

    pub fn mx_ticks_get(
        ) -> u64;

    pub fn mx_ticks_per_second(
        ) -> u64;

    pub fn mx_deadline_after(
        nanoseconds: mx_duration_t
        ) -> mx_time_t;

    pub fn mx_system_get_num_cpus(
        ) -> u32;

    pub fn mx_system_get_version(
        version: *mut u8,
        version_len: u32
        ) -> mx_status_t;

    pub fn mx_system_get_physmem(
        ) -> u64;

    pub fn mx_cache_flush(
        addr: *const u8,
        len: usize,
        options: u32
        ) -> mx_status_t;

    pub fn mx_handle_close(
        handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_handle_duplicate(
        handle: mx_handle_t,
        rights: mx_rights_t,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_handle_replace(
        handle: mx_handle_t,
        rights: mx_rights_t,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_object_wait_one(
        handle: mx_handle_t,
        waitfor: mx_signals_t,
        deadline: mx_time_t,
        observed: *mut mx_signals_t
        ) -> mx_status_t;

    pub fn mx_object_wait_many(
        items: *mut mx_wait_item_t,
        count: u32,
        deadline: mx_time_t
        ) -> mx_status_t;

    pub fn mx_object_wait_async(
        handle: mx_handle_t,
        port_handle: mx_handle_t,
        key: u64,
        signals: mx_signals_t,
        options: u32
        ) -> mx_status_t;

    pub fn mx_object_signal(
        handle: mx_handle_t,
        clear_mask: u32,
        set_mask: u32
        ) -> mx_status_t;

    pub fn mx_object_signal_peer(
        handle: mx_handle_t,
        clear_mask: u32,
        set_mask: u32
        ) -> mx_status_t;

    pub fn mx_object_get_property(
        handle: mx_handle_t,
        property: u32,
        value: *mut u8,
        size: usize
        ) -> mx_status_t;

    pub fn mx_object_set_property(
        handle: mx_handle_t,
        property: u32,
        value: *const u8,
        size: usize
        ) -> mx_status_t;

    pub fn mx_object_set_cookie(
        handle: mx_handle_t,
        scope: mx_handle_t,
        cookie: u64
        ) -> mx_status_t;

    pub fn mx_object_get_cookie(
        handle: mx_handle_t,
        scope: mx_handle_t,
        cookie: *mut u64
        ) -> mx_status_t;

    pub fn mx_object_get_info(
        handle: mx_handle_t,
        topic: u32,
        buffer: *mut u8,
        buffer_size: usize,
        actual_count: *mut usize,
        avail_count: *mut usize
        ) -> mx_status_t;

    pub fn mx_object_get_child(
        handle: mx_handle_t,
        koid: u64,
        rights: mx_rights_t,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_channel_create(
        options: u32,
        out0: *mut mx_handle_t,
        out1: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_channel_read(
        handle: mx_handle_t,
        options: u32,
        bytes: *mut u8,
        handles: *mut mx_handle_t,
        num_bytes: u32,
        num_handles: u32,
        actual_bytes: *mut u32,
        actual_handles: *mut u32
        ) -> mx_status_t;

    pub fn mx_channel_write(
        handle: mx_handle_t,
        options: u32,
        bytes: *const u8,
        num_bytes: u32,
        handles: *const mx_handle_t,
        num_handles: u32
        ) -> mx_status_t;

    pub fn mx_channel_call_noretry(
        handle: mx_handle_t,
        options: u32,
        deadline: mx_time_t,
        args: *const mx_channel_call_args_t,
        actual_bytes: *mut u32,
        actual_handles: *mut u32,
        read_status: *mut mx_status_t
        ) -> mx_status_t;

    pub fn mx_channel_call_finish(
        handle: mx_handle_t,
        deadline: mx_time_t,
        args: *const mx_channel_call_args_t,
        actual_bytes: *mut u32,
        actual_handles: *mut u32,
        read_status: *mut mx_status_t
        ) -> mx_status_t;

    pub fn mx_channel_call(
        handle: mx_handle_t,
        options: u32,
        deadline: mx_time_t,
        args: *const mx_channel_call_args_t,
        actual_bytes: *mut u32,
        actual_handles: *mut u32,
        read_status: *mut mx_status_t
        ) -> mx_status_t;

    pub fn mx_socket_create(
        options: u32,
        out0: *mut mx_handle_t,
        out1: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_socket_write(
        handle: mx_handle_t,
        options: u32,
        buffer: *const u8,
        size: usize,
        actual: *mut usize
        ) -> mx_status_t;

    pub fn mx_socket_read(
        handle: mx_handle_t,
        options: u32,
        buffer: *mut u8,
        size: usize,
        actual: *mut usize
        ) -> mx_status_t;

    pub fn mx_thread_exit(
        );

    pub fn mx_thread_create(
        process: mx_handle_t,
        name: *const u8,
        name_len: u32,
        options: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_thread_start(
        handle: mx_handle_t,
        thread_entry: usize,
        stack: usize,
        arg1: usize,
        arg2: usize
        ) -> mx_status_t;

    pub fn mx_thread_read_state(
        handle: mx_handle_t,
        kind: u32,
        buffer: *mut u8,
        len: u32,
        actual: *mut u32
        ) -> mx_status_t;

    pub fn mx_thread_write_state(
        handle: mx_handle_t,
        kind: u32,
        buffer: *const u8,
        buffer_len: u32
        ) -> mx_status_t;

    pub fn mx_process_exit(
        retcode: isize
        );

    pub fn mx_process_create(
        job: mx_handle_t,
        name: *const u8,
        name_len: u32,
        options: u32,
        proc_handle: *mut mx_handle_t,
        vmar_handle: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_process_start(
        process_handle: mx_handle_t,
        thread_handle: mx_handle_t,
        entry: usize,
        stack: usize,
        arg_handle: mx_handle_t,
        arg2: usize
        ) -> mx_status_t;

    pub fn mx_process_read_memory(
        proc_: mx_handle_t,
        vaddr: usize,
        buffer: *mut u8,
        len: usize,
        actual: *mut usize
        ) -> mx_status_t;

    pub fn mx_process_write_memory(
        proc_: mx_handle_t,
        vaddr: usize,
        buffer: *const u8,
        len: usize,
        actual: *mut usize
        ) -> mx_status_t;

    pub fn mx_job_create(
        parent_job: mx_handle_t,
        options: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_job_set_policy(
        job: mx_handle_t,
        options: u32,
        topic: u32,
        policy: *const u8,
        count: u32
        ) -> mx_status_t;

    pub fn mx_task_bind_exception_port(
        object: mx_handle_t,
        eport: mx_handle_t,
        key: u64,
        options: u32
        ) -> mx_status_t;

    pub fn mx_task_suspend(
        task_handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_task_resume(
        task_handle: mx_handle_t,
        options: u32
        ) -> mx_status_t;

    pub fn mx_task_kill(
        task_handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_event_create(
        options: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_eventpair_create(
        options: u32,
        out0: *mut mx_handle_t,
        out1: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_futex_wait(
        value_ptr: *mut mx_futex_t,
        current_value: isize,
        deadline: mx_time_t
        ) -> mx_status_t;

    pub fn mx_futex_wake(
        value_ptr: *const mx_futex_t,
        count: u32
        ) -> mx_status_t;

    pub fn mx_futex_requeue(
        wake_ptr: *mut mx_futex_t,
        wake_count: u32,
        current_value: isize,
        requeue_ptr: *mut mx_futex_t,
        requeue_count: u32
        ) -> mx_status_t;

    pub fn mx_port_create(
        options: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_port_queue(
        handle: mx_handle_t,
        packet: *const u8,
        size: usize
        ) -> mx_status_t;

    pub fn mx_port_wait(
        handle: mx_handle_t,
        deadline: mx_time_t,
        packet: *mut u8,
        size: usize
        ) -> mx_status_t;

    pub fn mx_port_cancel(
        handle: mx_handle_t,
        source: mx_handle_t,
        key: u64
        ) -> mx_status_t;

    pub fn mx_timer_create(
        options: u32,
        clock_id: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_timer_start(
        handle: mx_handle_t,
        deadline: mx_time_t,
        period: mx_duration_t,
        slack: mx_duration_t
        ) -> mx_status_t;

    pub fn mx_timer_cancel(
        handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_vmo_create(
        size: u64,
        options: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_vmo_read(
        handle: mx_handle_t,
        data: *mut u8,
        offset: u64,
        len: usize,
        actual: *mut usize
        ) -> mx_status_t;

    pub fn mx_vmo_write(
        handle: mx_handle_t,
        data: *const u8,
        offset: u64,
        len: usize,
        actual: *mut usize
        ) -> mx_status_t;

    pub fn mx_vmo_get_size(
        handle: mx_handle_t,
        size: *mut u64
        ) -> mx_status_t;

    pub fn mx_vmo_set_size(
        handle: mx_handle_t,
        size: u64
        ) -> mx_status_t;

    pub fn mx_vmo_op_range(
        handle: mx_handle_t,
        op: u32,
        offset: u64,
        size: u64,
        buffer: *mut u8,
        buffer_size: usize
        ) -> mx_status_t;

    pub fn mx_vmo_clone(
        handle: mx_handle_t,
        options: u32,
        offset: u64,
        size: u64,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_vmo_set_cache_policy(
        handle: mx_handle_t,
        cache_policy: u32
        ) -> mx_status_t;

    pub fn mx_vmar_allocate(
        parent_vmar_handle: mx_handle_t,
        offset: usize,
        size: usize,
        map_flags: u32,
        child_vmar: *mut mx_handle_t,
        child_addr: *mut usize
        ) -> mx_status_t;

    pub fn mx_vmar_destroy(
        vmar_handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_vmar_map(
        vmar_handle: mx_handle_t,
        vmar_offset: usize,
        vmo_handle: mx_handle_t,
        vmo_offset: u64,
        len: usize,
        map_flags: u32,
        mapped_addr: *mut usize
        ) -> mx_status_t;

    pub fn mx_vmar_unmap(
        vmar_handle: mx_handle_t,
        addr: usize,
        len: usize
        ) -> mx_status_t;

    pub fn mx_vmar_protect(
        vmar_handle: mx_handle_t,
        addr: usize,
        len: usize,
        prot_flags: u32
        ) -> mx_status_t;

    pub fn mx_cprng_draw(
        buffer: *mut u8,
        len: usize,
        actual: *mut usize
        ) -> mx_status_t;

    pub fn mx_cprng_add_entropy(
        buffer: *const u8,
        len: usize
        ) -> mx_status_t;

    pub fn mx_fifo_create(
        elem_count: u32,
        elem_size: u32,
        options: u32,
        out0: *mut mx_handle_t,
        out1: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_fifo_read(
        handle: mx_handle_t,
        data: *mut u8,
        len: usize,
        num_written: *mut u32
        ) -> mx_status_t;

    pub fn mx_fifo_write(
        handle: mx_handle_t,
        data: *const u8,
        len: usize,
        num_written: *mut u32
        ) -> mx_status_t;

    pub fn mx_vmar_unmap_handle_close_thread_exit(
        vmar_handle: mx_handle_t,
        addr: usize,
        len: usize,
        handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_futex_wake_handle_close_thread_exit(
        value_ptr: *const mx_futex_t,
        count: u32,
        new_value: isize,
        handle: mx_handle_t
        );

    pub fn mx_log_create(
        options: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_log_write(
        handle: mx_handle_t,
        len: u32,
        buffer: *const u8,
        options: u32
        ) -> mx_status_t;

    pub fn mx_log_read(
        handle: mx_handle_t,
        len: u32,
        buffer: *mut u8,
        options: u32
        ) -> mx_status_t;

    pub fn mx_ktrace_read(
        handle: mx_handle_t,
        data: *mut u8,
        offset: u32,
        len: u32,
        actual: *mut u32
        ) -> mx_status_t;

    pub fn mx_ktrace_control(
        handle: mx_handle_t,
        action: u32,
        options: u32,
        ptr: *mut u8
        ) -> mx_status_t;

    pub fn mx_ktrace_write(
        handle: mx_handle_t,
        id: u32,
        arg0: u32,
        arg1: u32
        ) -> mx_status_t;

    pub fn mx_mtrace_control(
        handle: mx_handle_t,
        kind: u32,
        action: u32,
        options: u32,
        ptr: *mut u8,
        size: u32
        ) -> mx_status_t;

    pub fn mx_debug_read(
        handle: mx_handle_t,
        buffer: *mut u8,
        length: u32
        ) -> mx_status_t;

    pub fn mx_debug_write(
        buffer: *const u8,
        length: u32
        ) -> mx_status_t;

    pub fn mx_debug_send_command(
        resource_handle: mx_handle_t,
        buffer: *const u8,
        length: u32
        ) -> mx_status_t;

    pub fn mx_interrupt_create(
        handle: mx_handle_t,
        vector: u32,
        options: u32
        ) -> mx_handle_t;

    pub fn mx_interrupt_complete(
        handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_interrupt_wait(
        handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_interrupt_signal(
        handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_mmap_device_io(
        handle: mx_handle_t,
        io_addr: u32,
        len: u32
        ) -> mx_status_t;

    pub fn mx_mmap_device_memory(
        handle: mx_handle_t,
        paddr: mx_paddr_t,
        len: u32,
        cache_policy: mx_cache_policy_t,
        out_vaddr: *mut usize
        ) -> mx_status_t;

    pub fn mx_io_mapping_get_info(
        handle: mx_handle_t,
        out_vaddr: *mut usize,
        out_size: *mut u64
        ) -> mx_status_t;

    pub fn mx_vmo_create_contiguous(
        rsrc_handle: mx_handle_t,
        size: usize,
        alignment_log2: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_bootloader_fb_get_info(
        format: *mut u32,
        width: *mut u32,
        height: *mut u32,
        stride: *mut u32
        ) -> mx_status_t;

    pub fn mx_set_framebuffer(
        handle: mx_handle_t,
        vaddr: *mut u8,
        len: u32,
        format: u32,
        width: u32,
        height: u32,
        stride: u32
        ) -> mx_status_t;

    pub fn mx_set_framebuffer_vmo(
        handle: mx_handle_t,
        vmo: mx_handle_t,
        len: u32,
        format: u32,
        width: u32,
        height: u32,
        stride: u32
        ) -> mx_status_t;

    pub fn mx_clock_adjust(
        handle: mx_handle_t,
        clock_id: u32,
        offset: i64
        ) -> mx_status_t;

    pub fn mx_pci_get_nth_device(
        handle: mx_handle_t,
        index: u32,
        out_info: *mut mx_pcie_device_info_t
        ) -> mx_handle_t;

    pub fn mx_pci_enable_bus_master(
        handle: mx_handle_t,
        enable: bool
        ) -> mx_status_t;

    pub fn mx_pci_enable_pio(
        handle: mx_handle_t,
        enable: bool
        ) -> mx_status_t;

    pub fn mx_pci_reset_device(
        handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_pci_get_bar(
        handle: mx_handle_t,
        bar_num: u32,
        out_bar: *mut mx_pci_resource_t
        ) -> mx_status_t;

    pub fn mx_pci_get_config(
        handle: mx_handle_t,
        out_config: *mut mx_pci_resource_t
        ) -> mx_status_t;

    pub fn mx_pci_io_write(
        handle: mx_handle_t,
        bar_num: u32,
        offset: u32,
        len: u32,
        value: u32
        ) -> mx_status_t;

    pub fn mx_pci_io_read(
        handle: mx_handle_t,
        bar_num: u32,
        offset: u32,
        len: u32,
        out_value: *mut u32
        ) -> mx_status_t;

    pub fn mx_pci_map_interrupt(
        handle: mx_handle_t,
        which_irq: i32,
        out_handle: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_pci_query_irq_mode_caps(
        handle: mx_handle_t,
        mode: u32,
        out_max_irqs: *mut u32
        ) -> mx_status_t;

    pub fn mx_pci_set_irq_mode(
        handle: mx_handle_t,
        mode: u32,
        requested_irq_count: u32
        ) -> mx_status_t;

    pub fn mx_pci_init(
        handle: mx_handle_t,
        init_buf: *const mx_pci_init_arg_t,
        len: u32
        ) -> mx_status_t;

    pub fn mx_pci_add_subtract_io_range(
        handle: mx_handle_t,
        mmio: bool,
        base: u64,
        len: u64,
        add: bool
        ) -> mx_status_t;

    pub fn mx_acpi_uefi_rsdp(
        handle: mx_handle_t
        ) -> u64;

    pub fn mx_acpi_cache_flush(
        handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_resource_create(
        parent_handle: mx_handle_t,
        records: *const mx_rrec_t,
        count: u32,
        resource_out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_resource_destroy(
        handle: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_resource_get_handle(
        handle: mx_handle_t,
        index: u32,
        options: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_resource_do_action(
        handle: mx_handle_t,
        index: u32,
        action: u32,
        arg0: u32,
        arg1: u32
        ) -> mx_status_t;

    pub fn mx_resource_connect(
        handle: mx_handle_t,
        channel: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_resource_accept(
        handle: mx_handle_t,
        channel: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_hypervisor_create(
        handle: mx_handle_t,
        options: u32,
        out: *mut mx_handle_t
        ) -> mx_status_t;

    pub fn mx_hypervisor_op(
        handle: mx_handle_t,
        opcode: u32,
        args: *const u8,
        args_len: u32,
        result: *mut u8,
        result_len: u32
        ) -> mx_status_t;

    pub fn mx_system_mexec(
        kernel: mx_handle_t,
        bootimage: mx_handle_t
        ) -> mx_status_t;

    pub fn mx_syscall_test_0(
        ) -> mx_status_t;

    pub fn mx_syscall_test_1(
        a: isize
        ) -> mx_status_t;

    pub fn mx_syscall_test_2(
        a: isize,
        b: isize
        ) -> mx_status_t;

    pub fn mx_syscall_test_3(
        a: isize,
        b: isize,
        c: isize
        ) -> mx_status_t;

    pub fn mx_syscall_test_4(
        a: isize,
        b: isize,
        c: isize,
        d: isize
        ) -> mx_status_t;

    pub fn mx_syscall_test_5(
        a: isize,
        b: isize,
        c: isize,
        d: isize,
        e: isize
        ) -> mx_status_t;

    pub fn mx_syscall_test_6(
        a: isize,
        b: isize,
        c: isize,
        d: isize,
        e: isize,
        f: isize
        ) -> mx_status_t;

    pub fn mx_syscall_test_7(
        a: isize,
        b: isize,
        c: isize,
        d: isize,
        e: isize,
        f: isize,
        g: isize
        ) -> mx_status_t;

    pub fn mx_syscall_test_8(
        a: isize,
        b: isize,
        c: isize,
        d: isize,
        e: isize,
        f: isize,
        g: isize,
        h: isize
        ) -> mx_status_t;

    pub fn mx_syscall_test_wrapper(
        a: isize,
        b: isize,
        c: isize
        ) -> mx_status_t;


}
