// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Included from other files to define a syscall api.

// NOTE: to determine number of arguments for any given syscall use the following algorithm:
//
// 64bit args are simply the number of arguments, since there are no special packing rules for simple
// integer args on 64bit architectures that magenta currently supports (arm64 and x86-64). All 64 and
// 32bit args use up a single slot.
//
// For 32bit, each function argument occupies one slot, unless it is a fixed 64bit size (uint64, mx_time_t),
// in which case it uses two. Additionally, for arm32, if the 64bit slot is now not in an even/odd slot,
// a padding slot needs to be added. Examples:
// uint32 == 1 slot
// uint64 == 2 slots
// uint32, uint32, uint64 == 4 slots
// uint32, uint64, uint32 == 5 slots (uint32, padding, uint64 (2 slots), uint32)

// Users of this file need to declare an implementation of the following macros:
// MAGENTA_SYSCALL_DEF(#64b slots, #32b slots, syscall #, return type, name, arguments...)
// MAGENTA_VDSOCALL_DEF(return type, name, arguments...)
//
// (optional)
// MAGENTA_SYSCALL_DEF_WITH_ATTRS(#64b slots, #32b slots, syscall #, return type, name, attrs, arguments...)
// MAGENTA_VDSOCALL_DEF_WITH_ATTRS(return type, name, attrs, arguments...)

#ifndef MAGENTA_SYSCALL_DEF
#error MAGENTA_SYSCALL_DEF not defined
#endif

#ifndef MAGENTA_VDSOCALL_DEF
#error MAGENTA_VDSOCALL_DEF not defined
#endif

#ifndef MAGENTA_SYSCALL_DEF_WITH_ATTRS
#define MAGENTA_SYSCALL_DEF_WITH_ATTRS(num_64bit_slots, num_32bit_slots, syscall_num, return_type, \
        name, attrs, arguments...) \
  MAGENTA_SYSCALL_DEF(num_64bit_slots, num_32bit_slots, syscall_num, return_type, name, arguments)
#endif

#ifndef MAGENTA_VDSOCALL_DEF_WITH_ATTRS
#define MAGENTA_VDSOCALL_DEF_WITH_ATTRS(ret, name, attrs, arguments...) \
    MAGENTA_VDSOCALL_DEF(ret, name, arguments)
#endif

#ifndef USER_PTR
#define USER_PTR(type) type*
#endif

// Time
MAGENTA_SYSCALL_DEF(0, 0, 0, mx_time_t, time_get, uint32_t clock_id)
MAGENTA_SYSCALL_DEF(1, 2, 1, mx_status_t, nanosleep, mx_time_t nanoseconds)

// Global system information
MAGENTA_VDSOCALL_DEF_WITH_ATTRS(uint32_t, num_cpus, (leaf, const), void)
MAGENTA_VDSOCALL_DEF(mx_status_t, version_get, USER_PTR(char) version, uint32_t version_len)

// Generic handle operations
MAGENTA_SYSCALL_DEF(1, 1, 10, mx_status_t, handle_close, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(3, 3, 11, mx_status_t, handle_duplicate, mx_handle_t handle,
                    mx_rights_t rights, USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(3, 3, 12, mx_status_t, handle_replace, mx_handle_t handle,
                    mx_rights_t rights, USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(4, 5, 13, mx_status_t, handle_wait_one, mx_handle_t handle, mx_signals_t waitfor,
                    mx_time_t timeout, USER_PTR(mx_signals_t) observed)
MAGENTA_SYSCALL_DEF(3, 4, 14, mx_status_t, handle_wait_many, USER_PTR(mx_wait_item_t) items,
                    uint32_t count, mx_time_t timeout)

// Generic object operations
MAGENTA_SYSCALL_DEF(3, 3, 20, mx_status_t, object_signal, mx_handle_t handle,
                    uint32_t clear_mask, uint32_t set_mask)
MAGENTA_SYSCALL_DEF(3, 3, 21, mx_status_t, object_signal_peer, mx_handle_t handle,
                    uint32_t clear_mask, uint32_t set_mask)
MAGENTA_SYSCALL_DEF(4, 4, 22, mx_status_t, object_get_property, mx_handle_t handle,
                    uint32_t property, USER_PTR(void) value, size_t size)
MAGENTA_SYSCALL_DEF(4, 4, 23, mx_status_t, object_set_property, mx_handle_t handle,
                    uint32_t property, USER_PTR(const void) value, size_t size)
MAGENTA_SYSCALL_DEF(6, 6, 24, mx_status_t, object_get_info, mx_handle_t handle,
                    uint32_t topic, USER_PTR(void) buffer, size_t buffer_size,
                    USER_PTR(size_t) actual_count, USER_PTR(size_t) avail_count)
MAGENTA_SYSCALL_DEF(4, 6, 25, mx_status_t, object_get_child, mx_handle_t handle,
                    uint64_t koid, mx_rights_t rights, USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(4, 5, 26, mx_status_t, object_bind_exception_port,
                    mx_handle_t object, mx_handle_t eport, uint64_t key, uint32_t options)

// IPC: Channels
MAGENTA_SYSCALL_DEF(3, 3, 30, mx_status_t, channel_create, uint32_t options,
                    USER_PTR(mx_handle_t) out0, USER_PTR(mx_handle_t) out1)
MAGENTA_SYSCALL_DEF(8, 8, 31, mx_status_t, channel_read, mx_handle_t handle, uint32_t options,
                    USER_PTR(void) bytes, uint32_t num_bytes, USER_PTR(uint32_t) actual_bytes,
                    USER_PTR(mx_handle_t) handles, uint32_t num_handles, USER_PTR(uint32_t) actual_handles)
MAGENTA_SYSCALL_DEF(6, 6, 32, mx_status_t, channel_write, mx_handle_t handle, uint32_t options,
                    USER_PTR(const void) bytes, uint32_t num_bytes,
                    USER_PTR(const mx_handle_t) handles, uint32_t num_handles)

// IPC: Sockets
MAGENTA_SYSCALL_DEF(3, 3, 33, mx_status_t, socket_create, uint32_t options,
                    USER_PTR(mx_handle_t) out0, USER_PTR(mx_handle_t) out1)
MAGENTA_SYSCALL_DEF(5, 5, 34, mx_status_t, socket_write, mx_handle_t handle, uint32_t options,
                    USER_PTR(const void) buffer, size_t len, USER_PTR(size_t) actual)
MAGENTA_SYSCALL_DEF(5, 5, 35, mx_status_t, socket_read, mx_handle_t handle, uint32_t options,
                    USER_PTR(void) buffer, size_t len, USER_PTR(size_t) actual)

// Threads
MAGENTA_SYSCALL_DEF_WITH_ATTRS(0, 0, 40, void, thread_exit, (noreturn), void)
MAGENTA_SYSCALL_DEF(5, 5, 41, mx_status_t, thread_create, mx_handle_t process,
                    USER_PTR(const char) name, uint32_t name_len, uint32_t options,
                    USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(5, 5, 42, mx_status_t, thread_start, mx_handle_t handle,
                    uintptr_t thread_entry, uintptr_t stack, uintptr_t arg1, uintptr_t arg2)
MAGENTA_SYSCALL_DEF(5, 5, 43, mx_status_t, thread_read_state, mx_handle_t handle,
                    uint32_t kind, USER_PTR(void) state, uint32_t len, USER_PTR(uint32_t) actual)
MAGENTA_SYSCALL_DEF(4, 4, 44, mx_status_t, thread_write_state, mx_handle_t handle,
                    uint32_t kind, USER_PTR(const void) state, uint32_t len)

// Processes
MAGENTA_SYSCALL_DEF_WITH_ATTRS(1, 1, 50, void, process_exit, (noreturn), int retcode)
MAGENTA_SYSCALL_DEF(5, 5, 51, mx_status_t, process_create, mx_handle_t job,
                    USER_PTR(const char) name, uint32_t name_len, uint32_t options,
                    USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(6, 6, 52, mx_status_t, process_start, mx_handle_t process_handle,
                    mx_handle_t thread_handle, uintptr_t entry, uintptr_t stack,
                    mx_handle_t arg_handle, uintptr_t arg2)
MAGENTA_SYSCALL_DEF(6, 7, 53, mx_status_t, process_map_vm, mx_handle_t proc_handle,
                    mx_handle_t vmo_handle, uint64_t offset, size_t len,
                    USER_PTR(uintptr_t) ptr, uint32_t options)
MAGENTA_SYSCALL_DEF(3, 3, 54, mx_status_t, process_unmap_vm, mx_handle_t proc_handle,
                    uintptr_t address, size_t len)
MAGENTA_SYSCALL_DEF(4, 4, 55, mx_status_t, process_protect_vm, mx_handle_t proc_handle,
                    uintptr_t address, size_t len, uint32_t prot)
MAGENTA_SYSCALL_DEF(5, 5, 56, mx_status_t, process_read_memory, mx_handle_t proc, uintptr_t vaddr,
                    USER_PTR(void) buffer, size_t len, USER_PTR(size_t) actual)
MAGENTA_SYSCALL_DEF(5, 5, 57, mx_status_t, process_write_memory, mx_handle_t proc, uintptr_t vaddr,
                    USER_PTR(const void) buffer, size_t len, USER_PTR(size_t) actual)

// Jobs
MAGENTA_SYSCALL_DEF(3, 3, 60, mx_status_t, job_create, mx_handle_t parent_job,
                    uint32_t options, USER_PTR(mx_handle_t) out)

// Tasks (Thread, Process, or Job)
MAGENTA_SYSCALL_DEF(2, 2, 61, mx_status_t, task_resume, mx_handle_t task_handle, uint32_t options)
MAGENTA_SYSCALL_DEF(1, 1, 62, mx_status_t, task_kill, mx_handle_t task_handle)

// Synchronization
MAGENTA_SYSCALL_DEF(2, 2, 70, mx_status_t, event_create, uint32_t options, USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(3, 3, 71, mx_status_t, eventpair_create, uint32_t options,
                    USER_PTR(mx_handle_t) out0, USER_PTR(mx_handle_t) out1)
MAGENTA_SYSCALL_DEF(3, 4, 72, mx_status_t, futex_wait,
                    USER_PTR(mx_futex_t) value_ptr, int current_value, mx_time_t timeout)
MAGENTA_SYSCALL_DEF(2, 2, 73, mx_status_t, futex_wake,
                    USER_PTR(mx_futex_t) value_ptr, uint32_t count)
MAGENTA_SYSCALL_DEF(5, 5, 74, mx_status_t, futex_requeue,
                    USER_PTR(mx_futex_t) wake_ptr, uint32_t wake_count, int current_value,
                    USER_PTR(mx_futex_t) requeue_ptr, uint32_t requeue_count)

// Waitsets
MAGENTA_SYSCALL_DEF(2, 2, 80, mx_status_t, waitset_create, uint32_t options, USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(4, 6, 81, mx_status_t, waitset_add, mx_handle_t waitset_handle, uint64_t cookie,
                    mx_handle_t handle, mx_signals_t signals)
MAGENTA_SYSCALL_DEF(2, 4, 82, mx_status_t, waitset_remove, mx_handle_t waitset_handle, uint64_t cookie)
MAGENTA_SYSCALL_DEF(4, 6, 83, mx_status_t, waitset_wait, mx_handle_t waitset_handle, mx_time_t timeout,
                    USER_PTR(mx_waitset_result_t) results, USER_PTR(uint32_t) count)

// Ports
MAGENTA_SYSCALL_DEF(2, 2, 90, mx_status_t, port_create, uint32_t options, USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(3, 3, 91, mx_status_t, port_queue, mx_handle_t handle,
                    USER_PTR(const void) packet, size_t size)
MAGENTA_SYSCALL_DEF(4, 6, 92, mx_status_t, port_wait, mx_handle_t handle, mx_time_t timeout,
                    USER_PTR(void) packet, size_t size)
MAGENTA_SYSCALL_DEF(4, 6, 93, mx_status_t, port_bind, mx_handle_t handle, uint64_t key,
                    mx_handle_t source, mx_signals_t signals)

// Memory management
MAGENTA_SYSCALL_DEF(3, 4, 100, mx_status_t, vmo_create, uint64_t size, uint32_t options,
                    USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(5, 6, 101, mx_status_t, vmo_read, mx_handle_t handle,
                    USER_PTR(void) data, uint64_t offset, size_t len, USER_PTR(size_t) actual)
MAGENTA_SYSCALL_DEF(5, 6, 102, mx_status_t, vmo_write, mx_handle_t handle,
                    USER_PTR(const void) data, uint64_t offset, size_t len, USER_PTR(size_t) actual)
MAGENTA_SYSCALL_DEF(2, 4, 103, mx_status_t, vmo_get_size, mx_handle_t handle, USER_PTR(uint64_t) size)
MAGENTA_SYSCALL_DEF(2, 4, 104, mx_status_t, vmo_set_size, mx_handle_t handle, uint64_t size)
MAGENTA_SYSCALL_DEF(6, 8, 105, mx_status_t, vmo_op_range, mx_handle_t handle, uint32_t op,
                    uint64_t offset, uint64_t size, USER_PTR(void) buffer, size_t buffer_size)

// Random Numbers
MAGENTA_SYSCALL_DEF(3, 3, 110, mx_status_t, cprng_draw,
                    USER_PTR(void) buffer, size_t len, USER_PTR(size_t) actual)
MAGENTA_SYSCALL_DEF(2, 2, 111, mx_status_t, cprng_add_entropy,
                    USER_PTR(const void) buffer, size_t len)


// ----------------------------------------------------------------------------------------
// Syscalls past this point are non-public
// Some currently do not require a handle to restrict access.
// Those will be modified or removed.
// These syscalls are *not* a stable API/ABI surface.
// ----------------------------------------------------------------------------------------

// Logging
MAGENTA_SYSCALL_DEF(1, 1, 130, mx_handle_t, log_create, uint32_t options)
MAGENTA_SYSCALL_DEF(4, 4, 131, mx_status_t, log_write, mx_handle_t handle, uint32_t len,
                    USER_PTR(const void) buffer, uint32_t options)
MAGENTA_SYSCALL_DEF(4, 4, 132, mx_status_t, log_read,
                    mx_handle_t handle, uint32_t len, USER_PTR(void) buffer, uint32_t options)

// Tracing
MAGENTA_SYSCALL_DEF(4, 4, 140, mx_status_t, ktrace_read, mx_handle_t handle,
                    USER_PTR(void) data, uint32_t offset, uint32_t len,
                    USER_PTR(uint32_t) actual)
MAGENTA_SYSCALL_DEF(4, 4, 141, mx_status_t, ktrace_control, mx_handle_t handle,
                    uint32_t action, uint32_t options, USER_PTR(void) ptr)
MAGENTA_SYSCALL_DEF(4, 4, 142, mx_status_t, ktrace_write, mx_handle_t handle,
                    uint32_t id, uint32_t arg0, uint32_t arg1)


// Syscalls to be removed / refactored
MAGENTA_SYSCALL_DEF(3, 3, 151, mx_status_t, thread_arch_prctl, mx_handle_t handle,
                    uint32_t op, USER_PTR(uintptr_t) value_ptr)
MAGENTA_SYSCALL_DEF(2, 2, 152, mx_handle_t, debug_transfer_handle,
                    mx_handle_t proc, mx_handle_t handle)

// Legacy LK debug syscalls.
MAGENTA_SYSCALL_DEF(3, 3, 160, mx_status_t, debug_read, mx_handle_t handle,
                    void* buffer, uint32_t length)
MAGENTA_SYSCALL_DEF(2, 2, 161, mx_status_t, debug_write, const void* buffer, uint32_t length)
MAGENTA_SYSCALL_DEF(3, 3, 162, mx_status_t, debug_send_command, mx_handle_t resource_handle,
                    const void* buffer, uint32_t length)

// DDK Syscalls: Interrupts
MAGENTA_SYSCALL_DEF(3, 3, 170, mx_handle_t, interrupt_create, mx_handle_t handle,
                    uint32_t vector, uint32_t options)
MAGENTA_SYSCALL_DEF(1, 1, 171, mx_status_t, interrupt_complete, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(1, 1, 172, mx_status_t, interrupt_wait, mx_handle_t handle)

// DDK Syscalls: MMIO and Ports
MAGENTA_SYSCALL_DEF(3, 3, 180, mx_status_t, mmap_device_io, mx_handle_t handle,
                    uint32_t io_addr, uint32_t len)
MAGENTA_SYSCALL_DEF(5, 5, 181, mx_status_t, mmap_device_memory, mx_handle_t handle,
                    mx_paddr_t paddr, uint32_t len, mx_cache_policy_t cache_policy,
                    USER_PTR(void *) out_vaddr)
MAGENTA_SYSCALL_DEF(3, 3, 182, mx_status_t, io_mapping_get_info, mx_handle_t handle,
                    USER_PTR(uintptr_t) out_vaddr, USER_PTR(uint64_t) out_size)
MAGENTA_SYSCALL_DEF(3, 3, 183, mx_status_t, vmo_create_contiguous, mx_handle_t rsrc_handle,
                    size_t size, USER_PTR(mx_handle_t) out)

// DDK Syscalls: Misc Info
MAGENTA_SYSCALL_DEF(4, 4, 190, mx_status_t, bootloader_fb_get_info, uint32_t* format,
                    uint32_t* width, uint32_t* height, uint32_t* stride)
MAGENTA_SYSCALL_DEF(7, 7, 191, mx_status_t, set_framebuffer, mx_handle_t handle,
                    void* vaddr, uint32_t len, uint32_t format,
                    uint32_t width, uint32_t height, uint32_t stride)
MAGENTA_SYSCALL_DEF(4, 4, 192, mx_status_t, clock_adjust, mx_handle_t handle,
                    uint32_t clock_id, int64_t offset)

// DDK Syscalls: PCI
MAGENTA_SYSCALL_DEF(3, 3, 200, mx_handle_t, pci_get_nth_device, mx_handle_t handle,
                    uint32_t index, mx_pcie_get_nth_info_t* out_info)
MAGENTA_SYSCALL_DEF(1, 1, 201, mx_status_t, pci_claim_device, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(2, 2, 202, mx_status_t, pci_enable_bus_master, mx_handle_t handle, bool enable)
MAGENTA_SYSCALL_DEF(1, 1, 203, mx_status_t, pci_reset_device, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(3, 3, 204, mx_handle_t, pci_map_mmio, mx_handle_t handle,
                    uint32_t bar_num, mx_cache_policy_t cache_policy)
MAGENTA_SYSCALL_DEF(5, 5, 205, mx_status_t, pci_io_write, mx_handle_t handle,
                    uint32_t bar_num, uint32_t offset, uint32_t len, uint32_t value)
MAGENTA_SYSCALL_DEF(5, 5, 206, mx_status_t, pci_io_read, mx_handle_t handle,
                    uint32_t bar_num, uint32_t offset, uint32_t len, uint32_t* out_value)
MAGENTA_SYSCALL_DEF(2, 2, 207, mx_handle_t, pci_map_interrupt, mx_handle_t handle, int32_t which_irq)
MAGENTA_SYSCALL_DEF(1, 1, 208, mx_handle_t, pci_map_config, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(4, 4, 209, mx_status_t, pci_query_irq_mode_caps, mx_handle_t handle,
                    uint32_t mode, uint32_t* out_max_irqs)
MAGENTA_SYSCALL_DEF(4, 4, 210, mx_status_t, pci_set_irq_mode, mx_handle_t handle,
                    uint32_t mode, uint32_t requested_irq_count)
MAGENTA_SYSCALL_DEF(3, 3, 211, mx_status_t, pci_init, mx_handle_t handle,
                    USER_PTR(const mx_pci_init_arg_t) init_buf, uint32_t len)
MAGENTA_SYSCALL_DEF(5, 7, 212, mx_status_t, pci_add_subtract_io_range, mx_handle_t handle,
                    bool mmio, uint64_t base, uint64_t len, bool add)
MAGENTA_SYSCALL_DEF(2, 2, 213, mx_status_t, pci_enable_pio, mx_handle_t handle, bool enable)

// DDK Syscalls: ACPI Glue
MAGENTA_SYSCALL_DEF(1, 1, 220, uint32_t, acpi_uefi_rsdp, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(1, 1, 221, mx_status_t, acpi_cache_flush, mx_handle_t handle)

// Resources
MAGENTA_SYSCALL_DEF(5, 5, 300, mx_status_t, resource_create, mx_handle_t parent_handle,
                    USER_PTR(const mx_rrec_t) records, uint32_t count,
                    USER_PTR(mx_handle_t) resource_out)
MAGENTA_SYSCALL_DEF(4, 4, 301, mx_status_t, resource_get_handle, mx_handle_t handle,
                    uint32_t index, uint32_t options, USER_PTR(mx_handle_t) out)
MAGENTA_SYSCALL_DEF(5, 5, 302, mx_status_t, resource_do_action, mx_handle_t handle,
                    uint32_t index, uint32_t action, uint32_t arg0, uint32_t arg1)
MAGENTA_SYSCALL_DEF(2, 2, 303, mx_status_t, resource_connect, mx_handle_t handle, mx_handle_t channel)
MAGENTA_SYSCALL_DEF(2, 2, 304, mx_status_t, resource_accept, mx_handle_t handle, USER_PTR(mx_handle_t) channel)

// syscall arg passing tests
MAGENTA_SYSCALL_DEF(0, 0, 20000, int, syscall_test_0, void)
MAGENTA_SYSCALL_DEF(1, 1, 20001, int, syscall_test_1, int a)
MAGENTA_SYSCALL_DEF(2, 2, 20002, int, syscall_test_2, int a, int b)
MAGENTA_SYSCALL_DEF(3, 3, 20003, int, syscall_test_3, int a, int b, int c)
MAGENTA_SYSCALL_DEF(4, 4, 20004, int, syscall_test_4, int a, int b, int c, int d)
MAGENTA_SYSCALL_DEF(5, 5, 20005, int, syscall_test_5, int a, int b, int c, int d, int e)
MAGENTA_SYSCALL_DEF(6, 6, 20006, int, syscall_test_6, int a, int b, int c, int d, int e, int f)
MAGENTA_SYSCALL_DEF(7, 7, 20007, int, syscall_test_7, int a, int b, int c, int d, int e, int f, int g)
MAGENTA_SYSCALL_DEF(8, 8, 20008, int, syscall_test_8, int a, int b, int c, int d, int e, int f, int g, int h)

#undef MAGENTA_SYSCALL_DEF
#undef MAGENTA_SYSCALL_DEF_WITH_ATTRS
#undef MAGENTA_VDSOCALL_DEF
#undef MAGENTA_VDSOCALL_DEF_WITH_ATTRS
