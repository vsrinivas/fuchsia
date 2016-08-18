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

#ifndef MAGENTA_DDKCALL_DEF
#error MAGENTA_DDKCALL_DEF not defined
#endif

#ifndef MAGENTA_SYSCALL_DEF_WITH_ATTRS
#define MAGENTA_SYSCALL_DEF_WITH_ATTRS(num_64bit_slots, num_32bit_slots, syscall_num, return_type, \
        name, attrs, arguments...) \
  MAGENTA_SYSCALL_DEF(num_64bit_slots, num_32bit_slots, syscall_num, return_type, name, arguments)
#endif

MAGENTA_SYSCALL_DEF_WITH_ATTRS(1, 1, 0, void, exit, (noreturn), int retcode)
MAGENTA_DDKCALL_DEF(2, 2, 1, int, debug_read, void* buffer, uint32_t length)
MAGENTA_SYSCALL_DEF(2, 2, 2, int, debug_write, const void* buffer, uint32_t length)
MAGENTA_SYSCALL_DEF(1, 2, 3, mx_status_t, nanosleep, mx_time_t nanoseconds)
MAGENTA_SYSCALL_DEF(0, 0, 4, mx_time_t, current_time, void)
MAGENTA_DDKCALL_DEF(2, 2, 5, int, debug_send_command, const void* buffer, uint32_t length)

// Temporary system calls before pending calls to get system info are created.
MAGENTA_SYSCALL_DEF(0, 0, 6, unsigned int, num_cpus, void)

// Logging
MAGENTA_SYSCALL_DEF(1, 1, 30, mx_handle_t, log_create, uint32_t flags)
MAGENTA_SYSCALL_DEF(4, 3, 31, mx_status_t, log_write, mx_handle_t handle, uint32_t len,
                    const void* buffer, uint32_t flags)
MAGENTA_SYSCALL_DEF(4, 3, 32, mx_status_t, log_read,
                    mx_handle_t handle, uint32_t len, void* buffer, uint32_t flags)

// Generic handle operations
MAGENTA_SYSCALL_DEF(1, 1, 40, mx_status_t, handle_close, mx_handle_t handle)
MAGENTA_SYSCALL_DEF(2, 2, 41, mx_handle_t, handle_duplicate, mx_handle_t handle, mx_rights_t rights)
MAGENTA_SYSCALL_DEF(4, 5, 42, mx_status_t, handle_wait_one, mx_handle_t handle, mx_signals_t signals,
                    mx_time_t timeout, mx_signals_state_t* signals_state)
MAGENTA_SYSCALL_DEF(6, 8, 43, mx_status_t, handle_wait_many, uint32_t count, const mx_handle_t* handles,
                    const mx_signals_t* signals, mx_time_t timeout, uint32_t* result_index,
                    mx_signals_state_t* signals_states)
MAGENTA_SYSCALL_DEF(4, 4, 44, mx_ssize_t, handle_get_info, mx_handle_t handle, uint32_t topic, void* info,
                    mx_size_t info_size)

// Generic object operations
MAGENTA_SYSCALL_DEF(3, 3, 46, mx_status_t, object_signal, mx_handle_t handle, uint32_t set_mask,
                    uint32_t clear_mask)

// Threads
MAGENTA_SYSCALL_DEF(4, 4, 50, mx_handle_t, thread_create, int (*entry)(void*), void* arg,
                    const char* name, uint32_t name_len)
MAGENTA_SYSCALL_DEF_WITH_ATTRS(0, 0, 51, void, thread_exit, (noreturn), void)
MAGENTA_SYSCALL_DEF(3, 3, 52, mx_status_t, thread_arch_prctl, mx_handle_t handle, uint32_t op,
                    uintptr_t* value_ptr)

// Messages
MAGENTA_SYSCALL_DEF(2, 2, 60, mx_status_t, message_pipe_create, mx_handle_t out_handles[2], uint32_t flags)
MAGENTA_SYSCALL_DEF(6, 6, 61, mx_status_t, message_read, mx_handle_t handle, void* bytes,
                    uint32_t* num_bytes, mx_handle_t* handles, uint32_t* num_handles, uint32_t flags)
MAGENTA_SYSCALL_DEF(6, 6, 62, mx_status_t, message_write, mx_handle_t handle, const void* bytes,
                    uint32_t num_bytes, const mx_handle_t* handles, uint32_t num_handles, uint32_t flags)

// Drivers
MAGENTA_DDKCALL_DEF(2, 2, 70, mx_handle_t, interrupt_event_create, uint32_t vector, uint32_t flags)
MAGENTA_DDKCALL_DEF(1, 1, 71, mx_status_t, interrupt_event_complete, mx_handle_t handle)
MAGENTA_DDKCALL_DEF(1, 1, 72, mx_status_t, interrupt_event_wait, mx_handle_t handle)

// Processes
MAGENTA_SYSCALL_DEF(2, 2, 80, mx_handle_t, process_create, const char* name, uint32_t name_len)
MAGENTA_SYSCALL_DEF(3, 3, 82, mx_status_t, process_start, mx_handle_t handle, mx_handle_t handle_arg,
                    mx_vaddr_t entry)
MAGENTA_SYSCALL_DEF(6, 7, 83, mx_status_t, process_vm_map, mx_handle_t proc_handle, mx_handle_t vmo_handle,
                    uint64_t offset, mx_size_t len, uintptr_t *ptr, uint32_t flags)
MAGENTA_SYSCALL_DEF(3, 3, 84, mx_status_t, process_vm_unmap, mx_handle_t proc_handle, uintptr_t address,
                    mx_size_t len)
MAGENTA_SYSCALL_DEF(4, 4, 85, mx_status_t, process_vm_protect, mx_handle_t proc_handle, uintptr_t address,
                    mx_size_t len, uint32_t prot);

// Synchronization
MAGENTA_SYSCALL_DEF(1, 1, 90, mx_handle_t, event_create, uint32_t options)
MAGENTA_SYSCALL_DEF(3, 4, 93, mx_status_t, futex_wait, int* value_ptr, int current_value, mx_time_t timeout)
MAGENTA_SYSCALL_DEF(2, 2, 94, mx_status_t, futex_wake, int* value_ptr, uint32_t count)
MAGENTA_SYSCALL_DEF(5, 5, 95, mx_status_t, futex_requeue, int* wake_ptr, uint32_t wake_count,
                    int current_value, int* requeue_ptr, uint32_t requeue_count)

// Memory management
MAGENTA_SYSCALL_DEF(1, 2, 100, mx_handle_t, vm_object_create, uint64_t size)
MAGENTA_SYSCALL_DEF(4, 5, 101, mx_ssize_t, vm_object_read, mx_handle_t handle, void *data, uint64_t offset,
                    mx_size_t len)
MAGENTA_SYSCALL_DEF(4, 5, 102, mx_ssize_t, vm_object_write, mx_handle_t handle, const void *data,
                    uint64_t offset, mx_size_t len)
MAGENTA_SYSCALL_DEF(2, 4, 103, mx_status_t, vm_object_get_size, mx_handle_t handle, uint64_t *size)
MAGENTA_SYSCALL_DEF(2, 4, 104, mx_status_t, vm_object_set_size, mx_handle_t handle, uint64_t size)

// temporary syscalls to access port and memory mapped devices
MAGENTA_DDKCALL_DEF(2, 2, 105, mx_status_t, mmap_device_io, uint32_t io_addr, uint32_t len)
MAGENTA_DDKCALL_DEF(3, 3, 106, mx_status_t, mmap_device_memory, mx_paddr_t paddr, uint32_t len,
                    void **out_vaddr)
MAGENTA_DDKCALL_DEF(3, 3, 107, mx_status_t, alloc_device_memory, uint32_t len, mx_paddr_t *out_paddr,
                    void **out_vaddr)

MAGENTA_SYSCALL_DEF(2, 2, 160, mx_ssize_t, cprng_draw, void* buffer, mx_size_t len);
// TODO(security)
MAGENTA_SYSCALL_DEF(2, 2, 161, mx_status_t, cprng_add_entropy, void* buffer, mx_size_t len);

// TODO(security)
MAGENTA_DDKCALL_DEF(4, 4, 170, mx_status_t, bootloader_fb_get_info, uint32_t* format, uint32_t* width,
                    uint32_t* height, uint32_t* stride)
MAGENTA_DDKCALL_DEF(6, 6, 171, mx_status_t, set_framebuffer, void* vaddr, uint32_t len, uint32_t format,
                    uint32_t width, uint32_t height, uint32_t stride)

// temporary pci syscalls
// TODO: remove when the pci driver moves to user space
MAGENTA_DDKCALL_DEF(2, 2, 180, mx_handle_t, pci_get_nth_device, uint32_t index,
                    mx_pcie_get_nth_info_t* out_info)
MAGENTA_DDKCALL_DEF(1, 1, 181, mx_status_t, pci_claim_device, mx_handle_t handle)
MAGENTA_DDKCALL_DEF(2, 2, 182, mx_status_t, pci_enable_bus_master, mx_handle_t handle, bool enable)
MAGENTA_DDKCALL_DEF(1, 1, 183, mx_status_t, pci_reset_device, mx_handle_t handle)
MAGENTA_DDKCALL_DEF(3, 3, 184, mx_handle_t, pci_map_mmio, mx_handle_t handle, uint32_t bar_num,
                    mx_cache_policy_t cache_policy)
MAGENTA_DDKCALL_DEF(5, 5, 185, mx_status_t, pci_io_write, mx_handle_t handle, uint32_t bar_num,
                    uint32_t offset, uint32_t len, const uint32_t value)
MAGENTA_DDKCALL_DEF(5, 5, 186, mx_status_t, pci_io_read, mx_handle_t handle, uint32_t bar_num,
                    uint32_t offset, uint32_t len, uint32_t* out_value)
MAGENTA_DDKCALL_DEF(2, 2, 187, mx_handle_t, pci_map_interrupt, mx_handle_t handle, int32_t which_irq)
MAGENTA_DDKCALL_DEF(1, 1, 188, mx_status_t, pci_interrupt_wait, mx_handle_t handle)
MAGENTA_DDKCALL_DEF(1, 1, 189, mx_handle_t, pci_map_config, mx_handle_t handle)
MAGENTA_DDKCALL_DEF(4, 4, 190, mx_status_t, pci_query_irq_mode_caps, mx_handle_t handle,
                    mx_pci_irq_mode_t mode, uint32_t* out_max_irqs)
MAGENTA_DDKCALL_DEF(4, 4, 191, mx_status_t, pci_set_irq_mode, mx_handle_t handle, mx_pci_irq_mode_t mode,
                    uint32_t requested_irq_count)

// I/O mapping objects
MAGENTA_DDKCALL_DEF(3, 3, 200, mx_status_t, io_mapping_get_info, mx_handle_t handle, void** out_vaddr,
                    uint64_t* out_size)

// exception handlers
// TODO(dje): This one can go away once we have a suitable object representing the system.
MAGENTA_SYSCALL_DEF(3, 5, 210, mx_status_t, set_system_exception_port,
                    mx_handle_t eport, uint64_t key, uint32_t options)
MAGENTA_SYSCALL_DEF(4, 5, 211, mx_status_t, set_exception_port,
                    mx_handle_t object, mx_handle_t eport, uint64_t key, uint32_t options)
// TODO(dje): The arguments here assume there will only ever be threads that
// we want to respond to exceptions on. IWBN to have an API that doesn't make
// that assumption.
MAGENTA_SYSCALL_DEF(3, 3, 212, mx_status_t, mark_exception_handled,
                    mx_handle_t process, mx_koid_t tid, mx_exception_status_t status)
// TODO(dje): Exception handlers get a pid in the exception report but need a process handle to
// reply to it. Thus we provide a lookup mechanism to go from pid to process handle. The lookup doesn't
// necessarily have to be directly accomplished via a syscall. This is for bootstrapping for now.
// Rights checking for this operation (however it is done) is an open issue. For the current support,
// one possibility is to attach the rights checking to the handle the report came from, which is intuitive.
// However that's just a handle of an ioport, hardly something one would use to adjudicate process lookup.
// Another is to attach the rights checking to the handle the exception port was assigned to. This works
// for most cases: thread, process, and (eventually) debugger. One still needs to provide for the system
// exception handler, which generally doesn't know about any particular process until it receives an
// exception from it. Perhaps the system exception handler process itself can have certain rights.
// So for now |handle| is either a thread or process handle or MX_HANDLE_INVALID for "self process"
// (for now).
MAGENTA_SYSCALL_DEF(2, 2, 213, mx_handle_t, process_debug, mx_handle_t handle, mx_koid_t pid)

// IO Ports
MAGENTA_SYSCALL_DEF(1, 1, 220, mx_handle_t, io_port_create, uint32_t options)
MAGENTA_SYSCALL_DEF(3, 3, 221, mx_status_t, io_port_queue, mx_handle_t handle,
                    const void* packet, mx_size_t size)
MAGENTA_SYSCALL_DEF(3, 3, 222, mx_status_t, io_port_wait, mx_handle_t handle,
                    void* packet, mx_size_t size)
MAGENTA_SYSCALL_DEF(4, 6, 223, mx_status_t, io_port_bind, mx_handle_t handle, uint64_t key,
                    mx_handle_t source, mx_signals_t signals)

// Data Pipe
MAGENTA_SYSCALL_DEF(4, 4, 230, mx_handle_t, data_pipe_create, uint32_t options, mx_size_t element_size,
                    mx_size_t capacity, mx_handle_t* handle)
MAGENTA_SYSCALL_DEF(4, 4, 231, mx_ssize_t, data_pipe_write, mx_handle_t handle, uint32_t flags,
                    mx_size_t requested, const void* buffer)
MAGENTA_SYSCALL_DEF(4, 4, 232, mx_ssize_t, data_pipe_read, mx_handle_t handle, uint32_t flags,
                    mx_size_t requested, void* buffer)
MAGENTA_SYSCALL_DEF(4, 4, 233, mx_ssize_t, data_pipe_begin_write, mx_handle_t handle, uint32_t flags,
                    mx_size_t requested,  uintptr_t* buffer)
MAGENTA_SYSCALL_DEF(2, 2, 234, mx_status_t, data_pipe_end_write, mx_handle_t handle, mx_size_t written)
MAGENTA_SYSCALL_DEF(4, 4, 235, mx_ssize_t, data_pipe_begin_read, mx_handle_t handle, uint32_t flags,
                    mx_size_t requested, uintptr_t* buffer)
MAGENTA_SYSCALL_DEF(2, 2, 236, mx_status_t, data_pipe_end_read, mx_handle_t handle, mx_size_t read)

// Wait sets
MAGENTA_SYSCALL_DEF(0, 0, 240, mx_handle_t, wait_set_create, void)
MAGENTA_SYSCALL_DEF(4, 6, 241, mx_status_t, wait_set_add, mx_handle_t wait_set_handle, mx_handle_t handle,
                    mx_signals_t signals, uint64_t cookie)
MAGENTA_SYSCALL_DEF(2, 4, 242, mx_status_t, wait_set_remove, mx_handle_t wait_set_handle, uint64_t cookie);
MAGENTA_SYSCALL_DEF(5, 7, 243, mx_status_t, wait_set_wait, mx_handle_t wait_set_handle, mx_time_t timeout,
                    uint32_t* num_results, mx_wait_set_result_t* results, uint32_t* max_results);

// Object Properties
MAGENTA_SYSCALL_DEF(4, 4, 250, mx_status_t, object_get_property, mx_handle_t handle, uint32_t property,
                    void* value, mx_size_t size)
MAGENTA_SYSCALL_DEF(4, 4, 251, mx_status_t, object_set_property, mx_handle_t handle, uint32_t property,
                    const void* value, mx_size_t size)

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
#undef MAGENTA_DDKCALL_DEF
#undef MAGENTA_SYSCALL_DEF_WITH_ATTRS
