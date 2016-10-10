// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

// Users of this file need to declare an implementation of the following macro:
// MAGENTA_SYSCALL_DEF(#64b slots, #32b slots, syscall #, return type, name, arguments...)
//
// (optional)
// MAGENTA_SYSCALL_DEF_WITH_ATTRS(#64b slots, #32b slots, syscall #, return type, name, attrs, arguments...)

#ifndef MAGENTA_SYSCALL_DEF
#error MAGENTA_SYSCALL_DEF not defined
#endif

#ifndef MAGENTA_SYSCALL_DEF_WITH_ATTRS
#define MAGENTA_SYSCALL_DEF_WITH_ATTRS(num_64bit_slots, num_32bit_slots, syscall_num, return_type, \
        name, attrs, arguments...) \
  MAGENTA_SYSCALL_DEF(num_64bit_slots, num_32bit_slots, syscall_num, return_type, name, arguments)
#endif

#ifndef USER_PTR
#define USER_PTR(type) type*
#endif

MAGENTA_SYSCALL_DEF_WITH_ATTRS(1, 1, 0, void, exit, (noreturn), int retcode)
MAGENTA_SYSCALL_DEF(3, 3, 1, int, debug_read, mx_handle_t, void* buffer, uint32_t length)
MAGENTA_SYSCALL_DEF(2, 2, 2, int, debug_write, const void* buffer, uint32_t length)
MAGENTA_SYSCALL_DEF(1, 2, 3, mx_status_t, nanosleep, mx_time_t nanoseconds)
MAGENTA_SYSCALL_DEF(0, 0, 4, mx_time_t, current_time, void)
MAGENTA_SYSCALL_DEF(3, 3, 5, int, debug_send_command, mx_handle_t, const void* buffer, uint32_t length)

// Temporary system calls before pending calls to get system info are created.
MAGENTA_SYSCALL_DEF(0, 0, 6, unsigned int, num_cpus, void)

// Temporary syscall to aid debuggers
MAGENTA_SYSCALL_DEF(2, 3, 7, mx_handle_t, debug_task_get_child, mx_handle_t handle, uint64_t koid)
MAGENTA_SYSCALL_DEF(2, 2, 8, mx_handle_t, debug_transfer_handle, mx_handle_t proc, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(4, 4, 9, mx_ssize_t, debug_read_memory, mx_handle_t proc, uintptr_t vaddr,
                    mx_size_t len, USER_PTR(void) buffer)
MAGENTA_SYSCALL_DEF(4, 4, 10, mx_ssize_t, debug_write_memory, mx_handle_t proc, uintptr_t vaddr,
                    mx_size_t len, USER_PTR(const void) buffer)

MAGENTA_SYSCALL_DEF(4, 4, 11, mx_ssize_t, ktrace_read, mx_handle_t handle, void* ptr, uint32_t off, uint32_t len)
MAGENTA_SYSCALL_DEF(4, 4, 12, mx_status_t, ktrace_control, mx_handle_t handle, uint32_t action, uint32_t options, USER_PTR(void))
MAGENTA_SYSCALL_DEF(4, 4, 13, void, ktrace_write, mx_handle_t handle, uint32_t id, uint32_t arg0, uint32_t arg1)

// Logging
MAGENTA_SYSCALL_DEF(1, 1, 30, mx_handle_t, log_create, uint32_t flags)
MAGENTA_SYSCALL_DEF(4, 3, 31, mx_status_t, log_write, mx_handle_t handle, uint32_t len,
                    USER_PTR(const void) buffer, uint32_t flags)
MAGENTA_SYSCALL_DEF(4, 3, 32, mx_status_t, log_read,
                    mx_handle_t handle, uint32_t len, USER_PTR(void) buffer, uint32_t flags)

// Generic handle operations
MAGENTA_SYSCALL_DEF(1, 1, 40, mx_status_t, handle_close, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(2, 2, 41, mx_handle_t, handle_duplicate, mx_handle_t handle, mx_rights_t rights)
MAGENTA_SYSCALL_DEF(4, 5, 42, mx_status_t, handle_wait_one, mx_handle_t handle, mx_signals_t signals,
                    mx_time_t timeout, USER_PTR(mx_signals_state_t) signals_state)
MAGENTA_SYSCALL_DEF(6, 8, 43, mx_status_t, handle_wait_many, uint32_t count, USER_PTR(const mx_handle_t) handles,
                    USER_PTR(const mx_signals_t) signals, mx_time_t timeout, USER_PTR(uint32_t) result_index,
                    USER_PTR(mx_signals_state_t) signals_states)
MAGENTA_SYSCALL_DEF(2, 2, 44, mx_handle_t, handle_replace, mx_handle_t handle, mx_rights_t rights)

// Generic object operations
MAGENTA_SYSCALL_DEF(3, 3, 45, mx_status_t, object_signal, mx_handle_t handle, uint32_t clear_mask,
                    uint32_t set_mask)
MAGENTA_SYSCALL_DEF(4, 4, 46, mx_ssize_t, object_get_info, mx_handle_t handle, uint32_t topic,
                    uint16_t topic_size, USER_PTR(void) buffer, mx_size_t buffer_size)

// Threads
MAGENTA_SYSCALL_DEF(4, 4, 50, mx_handle_t, thread_create, mx_handle_t process, USER_PTR(const char) name, uint32_t name_len, uint32_t flags)
MAGENTA_SYSCALL_DEF(5, 5, 51, mx_status_t, thread_start, mx_handle_t handle, uintptr_t thread_entry, uintptr_t stack, uintptr_t arg1, uintptr_t arg2)
MAGENTA_SYSCALL_DEF_WITH_ATTRS(0, 0, 52, void, thread_exit, (noreturn), void)
MAGENTA_SYSCALL_DEF(3, 3, 53, mx_status_t, thread_arch_prctl, mx_handle_t handle, uint32_t op, USER_PTR(uintptr_t) value_ptr)

// Messages
MAGENTA_SYSCALL_DEF(2, 2, 60, mx_status_t, msgpipe_create, USER_PTR(mx_handle_t) out_handles /* [2] */, uint32_t flags)
MAGENTA_SYSCALL_DEF(6, 6, 61, mx_status_t, msgpipe_read, mx_handle_t handle, USER_PTR(void) bytes,
                    USER_PTR(uint32_t) num_bytes, USER_PTR(mx_handle_t) handles, USER_PTR(uint32_t) num_handles,
                    uint32_t flags)
MAGENTA_SYSCALL_DEF(6, 6, 62, mx_status_t, msgpipe_write, mx_handle_t handle, USER_PTR(const void) bytes,
                    uint32_t num_bytes, USER_PTR(const mx_handle_t) handles, uint32_t num_handles, uint32_t flags)

// Drivers
MAGENTA_SYSCALL_DEF(3, 3, 70, mx_handle_t, interrupt_create, mx_handle_t handle, uint32_t vector, uint32_t flags)
MAGENTA_SYSCALL_DEF(1, 1, 71, mx_status_t, interrupt_complete, mx_handle_t handle)

// Processes
MAGENTA_SYSCALL_DEF(3, 3, 80, mx_handle_t, process_create, USER_PTR(const char) name, uint32_t name_len, uint32_t flags)
MAGENTA_SYSCALL_DEF(6, 6, 82, mx_status_t, process_start, mx_handle_t process_handle, mx_handle_t thread_handle, uintptr_t entry, uintptr_t stack, mx_handle_t arg_handle, uintptr_t arg2)
MAGENTA_SYSCALL_DEF(6, 7, 83, mx_status_t, process_map_vm, mx_handle_t proc_handle, mx_handle_t vmo_handle,
                    uint64_t offset, mx_size_t len, USER_PTR(uintptr_t) ptr, uint32_t flags)
MAGENTA_SYSCALL_DEF(3, 3, 84, mx_status_t, process_unmap_vm, mx_handle_t proc_handle, uintptr_t address,
                    mx_size_t len)
MAGENTA_SYSCALL_DEF(4, 4, 85, mx_status_t, process_protect_vm, mx_handle_t proc_handle, uintptr_t address,
                    mx_size_t len, uint32_t prot);

// Shared between process and threads
MAGENTA_SYSCALL_DEF(2, 2, 86, mx_status_t, task_resume, mx_handle_t task_handle, uint32_t options);
MAGENTA_SYSCALL_DEF(1, 1, 87, mx_status_t, task_kill, mx_handle_t task_handle);

// Synchronization
MAGENTA_SYSCALL_DEF(1, 1, 90, mx_handle_t, event_create, uint32_t options)
MAGENTA_SYSCALL_DEF(2, 2, 91, mx_status_t, eventpair_create, USER_PTR(mx_handle_t) out_handles /* [2] */,
                    uint32_t flags)
MAGENTA_SYSCALL_DEF(3, 4, 93, mx_status_t, futex_wait, USER_PTR(volatile mx_futex_t) value_ptr, mx_futex_t current_value, mx_time_t timeout)
MAGENTA_SYSCALL_DEF(2, 2, 94, mx_status_t, futex_wake, USER_PTR(volatile mx_futex_t) value_ptr, uint32_t count)
MAGENTA_SYSCALL_DEF(5, 5, 95, mx_status_t, futex_requeue, USER_PTR(volatile mx_futex_t) wake_ptr, uint32_t wake_count,
                    mx_futex_t current_value, USER_PTR(volatile mx_futex_t) requeue_ptr, uint32_t requeue_count)

// Memory management
MAGENTA_SYSCALL_DEF(1, 2, 100, mx_handle_t, vmo_create, uint64_t size)
MAGENTA_SYSCALL_DEF(4, 5, 101, mx_ssize_t, vmo_read, mx_handle_t handle, USER_PTR(void) data, uint64_t offset,
                    mx_size_t len)
MAGENTA_SYSCALL_DEF(4, 5, 102, mx_ssize_t, vmo_write, mx_handle_t handle, USER_PTR(const void) data,
                    uint64_t offset, mx_size_t len)
MAGENTA_SYSCALL_DEF(2, 4, 103, mx_status_t, vmo_get_size, mx_handle_t handle, USER_PTR(uint64_t) size)
MAGENTA_SYSCALL_DEF(2, 4, 104, mx_status_t, vmo_set_size, mx_handle_t handle, uint64_t size)
MAGENTA_SYSCALL_DEF(6, 8, 105, mx_status_t, vmo_op_range, mx_handle_t handle, uint32_t op,
                    uint64_t offset, uint64_t size, USER_PTR(void) buffer, mx_size_t buffer_size)

// temporary syscalls to access port and memory mapped devices
MAGENTA_SYSCALL_DEF(3, 3, 110, mx_status_t, mmap_device_io, mx_handle_t handle, uint32_t io_addr, uint32_t len)
MAGENTA_SYSCALL_DEF(5, 5, 111, mx_status_t, mmap_device_memory, mx_handle_t handle, mx_paddr_t paddr,
                    uint32_t len, mx_cache_policy_t cache_policy, USER_PTR(void *) out_vaddr)
MAGENTA_SYSCALL_DEF(4, 4, 112, mx_status_t, alloc_device_memory, mx_handle_t handle, uint32_t len,
                    mx_paddr_t *out_paddr, void **out_vaddr)

MAGENTA_SYSCALL_DEF(2, 2, 160, mx_ssize_t, cprng_draw, USER_PTR(void) buffer, mx_size_t len);
// TODO(security)
MAGENTA_SYSCALL_DEF(2, 2, 161, mx_status_t, cprng_add_entropy, USER_PTR(void) buffer, mx_size_t len);

// TODO(security)
MAGENTA_SYSCALL_DEF(4, 4, 170, mx_status_t, bootloader_fb_get_info, uint32_t* format, uint32_t* width,
                    uint32_t* height, uint32_t* stride)
MAGENTA_SYSCALL_DEF(7, 7, 171, mx_status_t, set_framebuffer, mx_handle_t handle, void* vaddr,
                    uint32_t len, uint32_t format, uint32_t width, uint32_t height, uint32_t stride)

// temporary pci syscalls
// TODO: remove when the pci driver moves to user space
MAGENTA_SYSCALL_DEF(3, 3, 180, mx_handle_t, pci_get_nth_device, mx_handle_t handle, uint32_t index,
                    mx_pcie_get_nth_info_t* out_info)
MAGENTA_SYSCALL_DEF(1, 1, 181, mx_status_t, pci_claim_device, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(2, 2, 182, mx_status_t, pci_enable_bus_master, mx_handle_t handle, bool enable)
MAGENTA_SYSCALL_DEF(1, 1, 183, mx_status_t, pci_reset_device, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(3, 3, 184, mx_handle_t, pci_map_mmio, mx_handle_t handle, uint32_t bar_num,
                    mx_cache_policy_t cache_policy)
MAGENTA_SYSCALL_DEF(5, 5, 185, mx_status_t, pci_io_write, mx_handle_t handle, uint32_t bar_num,
                    uint32_t offset, uint32_t len, const uint32_t value)
MAGENTA_SYSCALL_DEF(5, 5, 186, mx_status_t, pci_io_read, mx_handle_t handle, uint32_t bar_num,
                    uint32_t offset, uint32_t len, uint32_t* out_value)
MAGENTA_SYSCALL_DEF(2, 2, 187, mx_handle_t, pci_map_interrupt, mx_handle_t handle, int32_t which_irq)
MAGENTA_SYSCALL_DEF(1, 1, 188, mx_handle_t, pci_map_config, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(4, 4, 189, mx_status_t, pci_query_irq_mode_caps, mx_handle_t handle,
                    mx_pci_irq_mode_t mode, uint32_t* out_max_irqs)
MAGENTA_SYSCALL_DEF(4, 4, 190, mx_status_t, pci_set_irq_mode, mx_handle_t handle, mx_pci_irq_mode_t mode,
                    uint32_t requested_irq_count)
MAGENTA_SYSCALL_DEF(3, 3, 191, mx_status_t, pci_init, mx_handle_t handle, USER_PTR(mx_pci_init_arg_t) init_buf, uint32_t len)

// I/O mapping objects
MAGENTA_SYSCALL_DEF(3, 3, 200, mx_status_t, io_mapping_get_info, mx_handle_t handle, void** out_vaddr,
                    uint64_t* out_size)

// exception handlers
MAGENTA_SYSCALL_DEF(4, 5, 211, mx_status_t, object_bind_exception_port,
                    mx_handle_t object, mx_handle_t eport, uint64_t key, uint32_t options)

// IO Ports
MAGENTA_SYSCALL_DEF(1, 1, 220, mx_handle_t, port_create, uint32_t options)
MAGENTA_SYSCALL_DEF(3, 3, 221, mx_status_t, port_queue, mx_handle_t handle,
                    USER_PTR(const void) packet, mx_size_t size)
MAGENTA_SYSCALL_DEF(3, 3, 222, mx_status_t, port_wait, mx_handle_t handle,
                    USER_PTR(void) packet, mx_size_t size)
MAGENTA_SYSCALL_DEF(4, 6, 223, mx_status_t, port_bind, mx_handle_t handle, uint64_t key,
                    mx_handle_t source, mx_signals_t signals)

// Data Pipe
MAGENTA_SYSCALL_DEF(4, 4, 230, mx_handle_t, datapipe_create, uint32_t options, mx_size_t element_size,
                    mx_size_t capacity, USER_PTR(mx_handle_t) handle)
MAGENTA_SYSCALL_DEF(4, 4, 231, mx_ssize_t, datapipe_write, mx_handle_t handle, uint32_t flags,
                    mx_size_t requested, USER_PTR(const void) buffer)
MAGENTA_SYSCALL_DEF(4, 4, 232, mx_ssize_t, datapipe_read, mx_handle_t handle, uint32_t flags,
                    mx_size_t requested, USER_PTR(void) buffer)
MAGENTA_SYSCALL_DEF(3, 3, 233, mx_ssize_t, datapipe_begin_write, mx_handle_t handle, uint32_t flags,
                    USER_PTR(uintptr_t) buffer)
MAGENTA_SYSCALL_DEF(2, 2, 234, mx_status_t, datapipe_end_write, mx_handle_t handle, mx_size_t written)
MAGENTA_SYSCALL_DEF(3, 3, 235, mx_ssize_t, datapipe_begin_read, mx_handle_t handle, uint32_t flags,
                    USER_PTR(uintptr_t) buffer)
MAGENTA_SYSCALL_DEF(2, 2, 236, mx_status_t, datapipe_end_read, mx_handle_t handle, mx_size_t read)

// Wait sets
MAGENTA_SYSCALL_DEF(0, 0, 240, mx_handle_t, waitset_create, void)
MAGENTA_SYSCALL_DEF(4, 6, 241, mx_status_t, waitset_add, mx_handle_t waitset_handle, mx_handle_t handle,
                    mx_signals_t signals, uint64_t cookie)
MAGENTA_SYSCALL_DEF(2, 4, 242, mx_status_t, waitset_remove, mx_handle_t waitset_handle, uint64_t cookie)
MAGENTA_SYSCALL_DEF(5, 7, 243, mx_status_t, waitset_wait, mx_handle_t waitset_handle, mx_time_t timeout,
                    USER_PTR(uint32_t) num_results, USER_PTR(mx_waitset_result_t) results,
                    USER_PTR(uint32_t) max_results)

// Object Properties
MAGENTA_SYSCALL_DEF(4, 4, 250, mx_status_t, object_get_property, mx_handle_t handle, uint32_t property,
                    USER_PTR(void) value, mx_size_t size)
MAGENTA_SYSCALL_DEF(4, 4, 251, mx_status_t, object_set_property, mx_handle_t handle, uint32_t property,
                    USER_PTR(const void) value, mx_size_t size)

// Privileged syscalls for ACPI support
MAGENTA_SYSCALL_DEF(1, 1, 260, uint32_t, acpi_uefi_rsdp, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(1, 1, 261, mx_status_t, acpi_cache_flush, mx_handle_t handle)

// Sockets
MAGENTA_SYSCALL_DEF(2, 2, 280, mx_status_t, socket_create, mx_handle_t out_handles[2], uint32_t flags)
MAGENTA_SYSCALL_DEF(4, 4, 281, mx_ssize_t, socket_write, mx_handle_t handle, uint32_t flags,
                    mx_size_t size, USER_PTR(const void) buffer)
MAGENTA_SYSCALL_DEF(4, 4, 282, mx_ssize_t, socket_read, mx_handle_t handle, uint32_t flags,
                    mx_size_t size, USER_PTR(void) buffer)

// Debugger calls
MAGENTA_SYSCALL_DEF(4, 4, 290, mx_status_t, thread_read_state, mx_handle_t handle, uint32_t kind, USER_PTR(void) buffer, USER_PTR(uint32_t) buffer_len)
MAGENTA_SYSCALL_DEF(4, 4, 291, mx_status_t, thread_write_state, mx_handle_t handle, uint32_t kind, USER_PTR(const void) buffer, uint32_t buffer_len)

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
