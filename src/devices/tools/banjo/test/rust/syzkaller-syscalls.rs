// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.syscalls banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;


// C ABI compat


#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_handle_basic {
    pub koid: zircon::sys::zx_koid_t,
    pub rights: zircon::sys::zx_rights_t,
    pub type: u32,
    pub related_koid: zircon::sys::zx_koid_t,
    pub props: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_handle_count {
    pub handle_count: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_process_handle_stats {
    pub handle_count: [u32; 64 as usize],
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_process {
    pub return_code: i64,
    pub started: bool,
    pub exited: bool,
    pub debugger_attached: bool,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_job {
    pub return_code: i64,
    pub exited: bool,
    pub kill_on_oom: bool,
    pub debugger_attached: bool,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_thread {
    pub state: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_thread_stats {
    pub total_runtime: zircon::sys::zx_duration_t,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_task_stats {
    pub mem_mapped_bytes: usize,
    pub mem_private_bytes: usize,
    pub mem_shared_bytes: usize,
    pub mem_scaled_shared_bytes: usize,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_vmar {
    pub base: usize,
    pub len: usize,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_bti {
    pub minimum_contiguity: u64,
    pub aspace_size: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_socket {
    pub options: u32,
    pub rx_buf_max: usize,
    pub rx_buf_size: usize,
    pub rx_buf_available: usize,
    pub tx_buf_max: usize,
    pub tx_buf_size: usize,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_maps_mapping {
    pub mmu_flags: zircon::sys::zx_vm_option_t,
    pub vmo_koid: zircon::sys::zx_koid_t,
    pub vmo_offset: u64,
    pub committed_pages: usize,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_maps {
    pub name: [i8; 32 as usize],
    pub base: zircon::sys::zx_vaddr_t,
    pub size: usize,
    pub depth: usize,
    pub type: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_vmo {
    pub koid: zircon::sys::zx_koid_t,
    pub name: [i8; 32 as usize],
    pub size_bytes: u64,
    pub parent_koid: zircon::sys::zx_koid_t,
    pub num_children: usize,
    pub num_mappings: usize,
    pub share_count: usize,
    pub flags: u32,
    pub committed_bytes: u64,
    pub handle_rights: zircon::sys::zx_rights_t,
    pub cache_policy: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_cpu_stats {
    pub cpu_number: u32,
    pub flags: u32,
    pub idle_time: zircon::sys::zx_duration_t,
    pub reschedules: u64,
    pub context_switches: u64,
    pub irq_preempts: u64,
    pub preempts: u64,
    pub yields: u64,
    pub ints: u64,
    pub timer_ints: u64,
    pub timers: u64,
    pub page_faults: u64,
    pub exceptions: u64,
    pub syscalls: u64,
    pub reschedule_ipis: u64,
    pub generic_ipis: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_kmem_stats {
    pub total_bytes: u64,
    pub free_bytes: u64,
    pub wired_bytes: u64,
    pub total_heap_bytes: u64,
    pub free_heap_bytes: u64,
    pub vmo_bytes: u64,
    pub mmu_overhead_bytes: u64,
    pub ipc_bytes: u64,
    pub other_bytes: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_info_resource {
    pub kind: u32,
    pub flags: u32,
    pub base: u64,
    pub size: usize,
    pub name: [i8; 32 as usize],
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_wait_item {
    pub handle: zircon::sys::zx_handle_t,
    pub waitfor: zircon::sys::zx_signals_t,
    pub pending: zircon::sys::zx_signals_t,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_exception_header {
    pub size: u32,
    pub type: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_x86_64_exc_data {
    pub vector: u64,
    pub err_code: u64,
    pub cr2: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_arm64_exc_data {
    pub esr: u32,
    pub far: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_exception_context {

}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_exception_report {
    pub header: zx_exception_header,
    pub context: zx_exception_context,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_policy_basic {
    pub condition: u32,
    pub policy: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_policy_timer_slack {
    pub min_slack: zircon::sys::zx_duration_t,
    pub default_mode: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_packet_signal {
    pub trigger: zircon::sys::zx_signals_t,
    pub observed: zircon::sys::zx_signals_t,
    pub count: u64,
    pub reserved0: u64,
    pub reserved1: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_packet_exception {
    pub pid: u64,
    pub tid: u64,
    pub reserved0: u64,
    pub reserved1: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_packet_guest_bell {
    pub addr: zircon::sys::zx_gpaddr_t,
    pub reserved0: u64,
    pub reserved1: u64,
    pub reserved2: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_packet_guest_mem {
    pub addr: zircon::sys::zx_gpaddr_t,
    pub inst_len: u8,
    pub inst_buf: [i8; 15 as usize],
    pub default_operand_size: u8,
    pub reserved: [u8; 7 as usize],
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_packet_guest_io {
    pub port: u16,
    pub access_size: u8,
    pub input: bool,
    pub reserved0: u64,
    pub reserved1: u64,
    pub reserved2: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_packet_guest_vcpu {
    pub type: u8,
    pub reserved: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_packet_interrupt {
    pub timestamp: zircon::sys::zx_time_t,
    pub reserved0: u64,
    pub reserved1: u64,
    pub reserved2: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_packet_page_request {
    pub command: u16,
    pub flags: u16,
    pub reserved0: u32,
    pub offset: u64,
    pub length: u64,
    pub reserved1: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_port_packet {
    pub key: u64,
    pub type: u32,
    pub status: zircon::sys::zx_status_t,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_profile_scheduler {
    pub priority: i32,
    pub boost: u32,
    pub deboost: u32,
    pub quantum: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_profile_info {
    pub type: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_iommu_desc_dummy {
    pub reserved: u8,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_iommu_desc_intel {
    pub register_base: u64,
    pub pci_segment: u16,
    pub whole_segment: bool,
    pub scope_bytes: u8,
    pub reserved_memory_bytes: u16,
    pub reserved: [u8; 2 as usize],
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_pci_bar {
    pub id: u32,
    pub type: u32,
    pub size: usize,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_pcie_device_info {
    pub vendor_id: u16,
    pub device_id: u16,
    pub base_class: u8,
    pub sub_class: u8,
    pub program_interface: u8,
    pub revision_id: u8,
    pub bus_id: u8,
    pub dev_id: u8,
    pub func_id: u8,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_pci_init_arg {
    pub num_irqs: u32,
    pub addr_window_count: u32,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_smc_parameters {
    pub func_id: u32,
    pub arg1: u64,
    pub arg2: u64,
    pub arg3: u64,
    pub arg4: u64,
    pub arg5: u64,
    pub arg6: u64,
    pub client_id: u16,
    pub secure_os_id: u16,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_smc_result {
    pub arg0: u64,
    pub arg1: u64,
    pub arg2: u64,
    pub arg3: u64,
    pub arg6: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_vcpu_state {
    pub rax: u64,
    pub rcx: u64,
    pub rdx: u64,
    pub rbx: u64,
    pub rsp: u64,
    pub rbp: u64,
    pub rsi: u64,
    pub rdi: u64,
    pub r8: u64,
    pub r9: u64,
    pub r10: u64,
    pub r11: u64,
    pub r12: u64,
    pub r13: u64,
    pub r14: u64,
    pub r15: u64,
    pub rflags: u64,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_vcpu_io {
    pub access_size: u8,
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub struct zx_system_powerctl_arg {

}


#[repr(zircon::sys::zx_clock_t)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_clock_id_t {
    ZX_CLOCK_MONOTONIC = 0,
    ZX_CLOCK_UTC = 1,
    ZX_CLOCK_THREAD = 2,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_feature_kind_t {
    ZX_FEATURE_KIND_CPU = 0,
    ZX_FEATURE_KIND_HW_BREAKPOINT_COUNT = 1,
    ZX_FEATURE_KIND_HW_WATCHPOINT_COUNT = 2,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_event_kind_t {
    ZX_SYSTEM_EVENT_LOW_MEMORY = 0,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_cache_flush_options_t {
    ZX_CACHE_FLUSH_DATA = 1,
    ZX_CACHE_FLUSH_INVALIDATE = 2,
    ZX_CACHE_FLUSH_INSN = 4,
}
#[repr(zircon::sys::zx_rights_t)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_rights_t {
    ZX_RIGHT_NONE = 0,
    ZX_RIGHT_DUPLICATE = 1,
    ZX_RIGHT_TRANSFER = 2,
    ZX_RIGHT_READ = 4,
    ZX_RIGHT_WRITE = 8,
    ZX_RIGHT_EXECUTE = 16,
    ZX_RIGHT_MAP = 32,
    ZX_RIGHT_GET_PROPERTY = 64,
    ZX_RIGHT_SET_PROPERTY = 128,
    ZX_RIGHT_ENUMERATE = 256,
    ZX_RIGHT_DESTROY = 512,
    ZX_RIGHT_SET_POLICY = 1024,
    ZX_RIGHT_GET_POLICY = 2048,
    ZX_RIGHT_SIGNAL = 4096,
    ZX_RIGHT_SIGNAL_PEER = 8192,
    ZX_RIGHT_WAIT = 16384,
    ZX_RIGHT_INSPECT = 32768,
    ZX_RIGHT_MANAGE_JOB = 65536,
    ZX_RIGHT_MANAGE_PROCESS = 131072,
    ZX_RIGHT_MANAGE_THREAD = 262144,
    ZX_RIGHT_APPLY_PROFILE = 524288,
    ZX_RIGHT_SAME_RIGHTS = 2147483648,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_object_wait_async_options_t {
    ZX_WAIT_ASYNC_ONCE = 0,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_object_property_values_t {
    ZX_PROP_NAME = 3,
    ZX_PROP_PROCESS_DEBUG_ADDR = 5,
    ZX_PROP_PROCESS_VDSO_BASE_ADDRESS = 6,
    ZX_PROP_SOCKET_RX_THRESHOLD = 12,
    ZX_PROP_SOCKET_TX_THRESHOLD = 13,
    ZX_PROP_JOB_KILL_ON_OOM = 15,
    ZX_PROP_EXCEPTION_STATE = 16,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_object_info_topics_t {
    ZX_INFO_NONE = 0,
    ZX_INFO_HANDLE_VALID = 1,
    ZX_INFO_HANDLE_BASIC = 2,
    ZX_INFO_PROCESS = 3,
    ZX_INFO_PROCESS_THREADS = 4,
    ZX_INFO_VMAR = 7,
    ZX_INFO_JOB_CHILDREN = 8,
    ZX_INFO_JOB_PROCESSES = 9,
    ZX_INFO_THREAD = 10,
    ZX_INFO_THREAD_EXCEPTION_REPORT = 11,
    ZX_INFO_TASK_STATS = 12,
    ZX_INFO_PROCESS_MAPS = 13,
    ZX_INFO_PROCESS_VMOS = 14,
    ZX_INFO_THREAD_STATS = 15,
    ZX_INFO_CPU_STATS = 16,
    ZX_INFO_KMEM_STATS = 17,
    ZX_INFO_RESOURCE = 18,
    ZX_INFO_HANDLE_COUNT = 19,
    ZX_INFO_BTI = 20,
    ZX_INFO_PROCESS_HANDLE_STATS = 21,
    ZX_INFO_SOCKET = 22,
    ZX_INFO_VMO = 23,
    ZX_INFO_JOB = 24,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_obj_props_t {
    ZX_OBJ_PROP_NONE = 0,
    ZX_OBJ_PROP_WAITABLE = 1,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_obj_types_t {
    ZX_OBJ_TYPE_NONE = 0,
    ZX_OBJ_TYPE_PROCESS = 1,
    ZX_OBJ_TYPE_THREAD = 2,
    ZX_OBJ_TYPE_VMO = 3,
    ZX_OBJ_TYPE_CHANNEL = 4,
    ZX_OBJ_TYPE_EVENT = 5,
    ZX_OBJ_TYPE_PORT = 6,
    ZX_OBJ_TYPE_INTERRUPT = 9,
    ZX_OBJ_TYPE_PCI_DEVICE = 11,
    ZX_OBJ_TYPE_LOG = 12,
    ZX_OBJ_TYPE_SOCKET = 14,
    ZX_OBJ_TYPE_RESOURCE = 15,
    ZX_OBJ_TYPE_EVENTPAIR = 16,
    ZX_OBJ_TYPE_JOB = 17,
    ZX_OBJ_TYPE_VMAR = 18,
    ZX_OBJ_TYPE_FIFO = 19,
    ZX_OBJ_TYPE_GUEST = 20,
    ZX_OBJ_TYPE_VCPU = 21,
    ZX_OBJ_TYPE_TIMER = 22,
    ZX_OBJ_TYPE_IOMMU = 23,
    ZX_OBJ_TYPE_BTI = 24,
    ZX_OBJ_TYPE_PROFILE = 25,
    ZX_OBJ_TYPE_PMT = 26,
    ZX_OBJ_TYPE_SUSPEND_TOKEN = 27,
    ZX_OBJ_TYPE_PAGER = 28,
    ZX_OBJ_TYPE_EXCEPTION = 29,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_thread_state_values_t {
    ZX_THREAD_STATE_NEW = 0,
    ZX_THREAD_STATE_RUNNING = 1,
    ZX_THREAD_STATE_SUSPENDED = 2,
    ZX_THREAD_STATE_BLOCKED = 3,
    ZX_THREAD_STATE_DYING = 4,
    ZX_THREAD_STATE_DEAD = 5,
    ZX_THREAD_STATE_BLOCKED_EXCEPTION = 259,
    ZX_THREAD_STATE_BLOCKED_SLEEPING = 515,
    ZX_THREAD_STATE_BLOCKED_FUTEX = 771,
    ZX_THREAD_STATE_BLOCKED_PORT = 1027,
    ZX_THREAD_STATE_BLOCKED_CHANNEL = 1283,
    ZX_THREAD_STATE_BLOCKED_WAIT_ONE = 1539,
    ZX_THREAD_STATE_BLOCKED_WAIT_MANY = 1795,
    ZX_THREAD_STATE_BLOCKED_INTERRUPT = 2051,
    ZX_THREAD_STATE_BLOCKED_PAGER = 2307,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_exception_channel_types_t {
    ZX_EXCEPTION_CHANNEL_TYPE_NONE = 0,
    ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER = 1,
    ZX_EXCEPTION_CHANNEL_TYPE_THREAD = 2,
    ZX_EXCEPTION_CHANNEL_TYPE_PROCESS = 3,
    ZX_EXCEPTION_CHANNEL_TYPE_JOB = 4,
    ZX_EXCEPTION_CHANNEL_TYPE_JOB_DEBUGGER = 5,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_exception_port_types_t {
    ZX_EXCEPTION_PORT_TYPE_NONE = 0,
    ZX_EXCEPTION_PORT_TYPE_DEBUGGER = 1,
    ZX_EXCEPTION_PORT_TYPE_THREAD = 2,
    ZX_EXCEPTION_PORT_TYPE_PROCESS = 3,
    ZX_EXCEPTION_PORT_TYPE_JOB = 4,
    ZX_EXCEPTION_PORT_TYPE_JOB_DEBUGGER = 5,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_info_maps_type_vals_t {
    ZX_INFO_MAPS_TYPE_NONE = 0,
    ZX_INFO_MAPS_TYPE_ASPACE = 1,
    ZX_INFO_MAPS_TYPE_VMAR = 2,
    ZX_INFO_MAPS_TYPE_MAPPING = 3,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_cache_policy_options_t {
    ZX_CACHE_POLICY_CACHED = 0,
    ZX_CACHE_POLICY_UNCACHED = 1,
    ZX_CACHE_POLICY_UNCACHED_DEVICE = 2,
    ZX_CACHE_POLICY_WRITE_COMBINING = 3,
    ZX_CACHE_POLICY_MASK = 3,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_info_vmo_type_vals_t {
    ZX_INFO_VMO_TYPE_PAGED = 1,
    ZX_INFO_VMO_TYPE_PHYSICAL = 0,
    ZX_INFO_VMO_RESIZABLE = 2,
    ZX_INFO_VMO_IS_COW_CLONE = 4,
    ZX_INFO_VMO_VIA_HANDLE = 8,
    ZX_INFO_VMO_VIA_MAPPING = 16,
    ZX_INFO_VMO_PAGER_BACKED = 32,
    ZX_INFO_VMO_CONTIGUOUS = 64,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_socket_create_options_t {
    ZX_SOCKET_STREAM = 0,
    ZX_SOCKET_DATAGRAM = 1,
    ZX_SOCKET_HAS_CONTROL = 2,
    ZX_SOCKET_HAS_ACCEPT = 4,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_socket_read_options_t {
    ZX_SOCKET_CONTROL = 4,
    ZX_SOCKET_PEEK = 8,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_socket_write_options_t {
    ZX_SOCKET_CONTROL = 4,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_socket_shutdown_options_t {
    ZX_SOCKET_SHUTDOWN_WRITE = 1,
    ZX_SOCKET_SHUTDOWN_READ = 2,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum thread_state_kind_t {
    ZX_THREAD_STATE_GENERAL_REGS = 0,
    ZX_THREAD_STATE_FP_REGS = 1,
    ZX_THREAD_STATE_VECTOR_REGS = 2,
    ZX_THREAD_STATE_DEBUG_REGS = 4,
    ZX_THREAD_STATE_SINGLE_STEP = 5,
    ZX_THREAD_X86_REGISTER_FS = 6,
    ZX_THREAD_X86_REGISTER_GS = 7,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_job_policy_options_t {
    ZX_JOB_POL_RELATIVE = 0,
    ZX_JOB_POL_ABSOLUTE = 1,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_job_policy_topic_t {
    ZX_JOB_POL_BASIC = 0,
    ZX_JOB_POL_TIMER_SLACK = 1,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_job_policy_conditions_t {
    ZX_POL_BAD_HANDLE = 0,
    ZX_POL_WRONG_OBJECT = 1,
    ZX_POL_VMAR_WX = 2,
    ZX_POL_NEW_ANY = 3,
    ZX_POL_NEW_VMO = 4,
    ZX_POL_NEW_CHANNEL = 5,
    ZX_POL_NEW_EVENT = 6,
    ZX_POL_NEW_EVENTPAIR = 7,
    ZX_POL_NEW_PORT = 8,
    ZX_POL_NEW_SOCKET = 9,
    ZX_POL_NEW_FIFO = 10,
    ZX_POL_NEW_TIMER = 11,
    ZX_POL_NEW_PROCESS = 12,
    ZX_POL_NEW_PROFILE = 13,
    ZX_POL_AMBIENT_MARK_VMO_EXEC = 14,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_job_policy_actions_t {
    ZX_POL_ACTION_ALLOW = 0,
    ZX_POL_ACTION_DENY = 1,
    ZX_POL_ACTION_ALLOW_EXCEPTION = 2,
    ZX_POL_ACTION_DENY_EXCEPTION = 3,
    ZX_POL_ACTION_KILL = 4,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_task_exception_port_options_t {
    ZX_EXCEPTION_PORT_DEBUGGER = 1,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_task_resume_options_t {
    ZX_RESUME_TRY_NEXT = 2,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_port_create_options_t {
    ZX_PORT_BIND_TO_INTERRUPT = 1,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_port_packet_type_t {
    ZX_PKT_TYPE_USER = 0,
    ZX_PKT_TYPE_SIGNAL_ONE = 1,
    ZX_PKT_TYPE_SIGNAL_REP = 2,
    ZX_PKT_TYPE_GUEST_BELL = 3,
    ZX_PKT_TYPE_GUEST_MEM = 4,
    ZX_PKT_TYPE_GUEST_IO = 5,
    ZX_PKT_TYPE_GUEST_VCPU = 6,
    ZX_PKT_TYPE_INTERRUPT = 7,
    ZX_PKT_TYPE_PAGE_REQUEST = 9,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_timer_options_t {
    ZX_TIMER_SLACK_CENTER = 0,
    ZX_TIMER_SLACK_EARLY = 1,
    ZX_TIMER_SLACK_LATE = 2,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_vmo_create_options_t {
    ZX_VMO_RESIZABLE = 2,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_vmo_opcodes_t {
    ZX_VMO_OP_COMMIT = 1,
    ZX_VMO_OP_DECOMMIT = 2,
    ZX_VMO_OP_LOCK = 3,
    ZX_VMO_OP_UNLOCK = 4,
    ZX_VMO_OP_CACHE_SYNC = 6,
    ZX_VMO_OP_CACHE_INVALIDATE = 7,
    ZX_VMO_OP_CACHE_CLEAN = 8,
    ZX_VMO_OP_CACHE_CLEAN_INVALIDATE = 9,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_vmo_cache_policy_options_t {
    ZX_CACHE_POLICY_CACHED = 0,
    ZX_CACHE_POLICY_UNCACHED = 1,
    ZX_CACHE_POLICY_UNCACHED_DEVICE = 2,
    ZX_CACHE_POLICY_WRITE_COMBINING = 3,
    ZX_CACHE_POLICY_MASK = 3,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_vmo_clone_options_t {
    ZX_VMO_CLONE_COPY_ON_WRITE = 1,
    ZX_VMO_CHILD_COPY_ON_WRITE = 1,
    ZX_VMO_CHILD_RESIZABLE = 4,
}
#[repr(zircon::sys::zx_vm_option_t)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_vmar_allocate_map_options_t {
    ZX_VM_PERM_READ = 1,
    ZX_VM_PERM_WRITE = 2,
    ZX_VM_PERM_EXECUTE = 4,
    ZX_VM_COMPACT = 8,
    ZX_VM_SPECIFIC = 16,
    ZX_VM_SPECIFIC_OVERWRITE = 32,
    ZX_VM_CAN_MAP_SPECIFIC = 64,
    ZX_VM_CAN_MAP_READ = 128,
    ZX_VM_CAN_MAP_WRITE = 256,
    ZX_VM_CAN_MAP_EXECUTE = 512,
    ZX_VM_MAP_RANGE = 1024,
    ZX_VM_REQUIRE_NON_RESIZABLE = 2048,
    ZX_VM_ALLOW_FAULTS = 4096,
    ZX_VM_ALIGN_1KB = 167772160,
    ZX_VM_ALIGN_2KB = 184549376,
    ZX_VM_ALIGN_4KB = 201326592,
    ZX_VM_ALIGN_8KB = 218103808,
    ZX_VM_ALIGN_16KB = 234881024,
    ZX_VM_ALIGN_32KB = 251658240,
    ZX_VM_ALIGN_64KB = 268435456,
    ZX_VM_ALIGN_128KB = 285212672,
    ZX_VM_ALIGN_256KB = 301989888,
    ZX_VM_ALIGN_512KB = 318767104,
    ZX_VM_ALIGN_1MB = 335544320,
    ZX_VM_ALIGN_2MB = 352321536,
    ZX_VM_ALIGN_4MB = 369098752,
    ZX_VM_ALIGN_8MB = 385875968,
    ZX_VM_ALIGN_16MB = 402653184,
    ZX_VM_ALIGN_32MB = 419430400,
    ZX_VM_ALIGN_64MB = 436207616,
    ZX_VM_ALIGN_128MB = 452984832,
    ZX_VM_ALIGN_256MB = 469762048,
    ZX_VM_ALIGN_512MB = 486539264,
    ZX_VM_ALIGN_1GB = 503316480,
    ZX_VM_ALIGN_2GB = 520093696,
    ZX_VM_ALIGN_4GB = 536870912,
}
#[repr(zircon::sys::zx_vm_option_t)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_vmar_protect_options_t {
    ZX_VM_PERM_READ = 1,
    ZX_VM_PERM_WRITE = 2,
    ZX_VM_PERM_EXECUTE = 4,
}
#[repr(i32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_profile_scheduler_priority_t {
    ZX_PRIORITY_LOWEST = 0,
    ZX_PRIORITY_LOW = 8,
    ZX_PRIORITY_DEFAULT = 16,
    ZX_PRIORITY_HIGH = 24,
    ZX_PRIORITY_HIGHEST = 31,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_profile_info_type_t {
    ZX_PROFILE_INFO_SCHEDULER = 1,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_debuglog_create_options_t {
    ZX_LOG_FLAG_READABLE = 1073741824,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_interrupt_options_t {
    ZX_INTERRUPT_REMAP_IRQ = 1,
    ZX_INTERRUPT_MODE_DEFAULT = 0,
    ZX_INTERRUPT_MODE_EDGE_LOW = 2,
    ZX_INTERRUPT_MODE_EDGE_HIGH = 4,
    ZX_INTERRUPT_MODE_LEVEL_LOW = 6,
    ZX_INTERRUPT_MODE_LEVEL_HIGH = 8,
    ZX_INTERRUPT_MODE_EDGE_BOTH = 10,
    ZX_INTERRUPT_MODE_MASK = 14,
    ZX_INTERRUPT_VIRTUAL = 16,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_iommu_type_t {
    ZX_IOMMU_TYPE_DUMMY = 0,
    ZX_IOMMU_TYPE_INTEL = 1,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_bti_pin_options_t {
    ZX_BTI_PERM_READ = 1,
    ZX_BTI_PERM_WRITE = 2,
    ZX_BTI_PERM_EXECUTE = 4,
    ZX_BTI_COMPRESS = 8,
    ZX_BTI_CONTIGUOUS = 16,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_rsrc_kind_vals_t {
    ZX_RSRC_KIND_MMIO = 0,
    ZX_RSRC_KIND_IRQ = 1,
    ZX_RSRC_KIND_IOPORT = 2,
    ZX_RSRC_KIND_HYPERVISOR = 3,
    ZX_RSRC_KIND_ROOT = 4,
    ZX_RSRC_KIND_VMEX = 5,
    ZX_RSRC_KIND_SMC = 6,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_guest_trap_kind_t {
    ZX_GUEST_TRAP_BELL = 0,
    ZX_GUEST_TRAP_MEM = 1,
    ZX_GUEST_TRAP_IO = 2,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_vcpu_read_state_kind_t {
    ZX_VCPU_STATE = 0,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_vcpu_write_state_kind_t {
    ZX_VCPU_STATE = 0,
    ZX_VCPU_IO = 1,
}
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_system_powerctl_cmd_t {
    ZX_SYSTEM_POWERCTL_ENABLE_ALL_CPUS = 1,
    ZX_SYSTEM_POWERCTL_DISABLE_ALL_CPUS_BUT_PRIMARY = 2,
    ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE = 3,
    ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1 = 4,
    ZX_SYSTEM_POWERCTL_REBOOT = 5,
    ZX_SYSTEM_POWERCTL_REBOOT_BOOTLOADER = 6,
    ZX_SYSTEM_POWERCTL_REBOOT_RECOVERY = 7,
    ZX_SYSTEM_POWERCTL_SHUTDOWN = 8,
}


#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_object_property_handles {
    pub type1: zircon::sys::zx_handle_t,
    pub type2: zircon::sys::zx_handle_t,
    pub type3: zircon::sys::zx_handle_t,
    pub type4: zircon::sys::zx_handle_t,
    pub type5: zircon::sys::zx_handle_t,
    pub type6: zircon::sys::zx_handle_t,
    pub type7: zircon::sys::zx_handle_t,
}

#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_object_info_types {
    pub type1: [bool; 1 as usize],
    pub type2: [bool; 1 as usize],
    pub type3: [zx_info_handle_basic; 1 as usize],
    pub type4: [zx_info_process; 1 as usize],
    pub type5: [zircon::sys::zx_koid_t; UNKNOWN as usize],
    pub type6: [zx_info_vmar; 1 as usize],
    pub type7: [zircon::sys::zx_koid_t; UNKNOWN as usize],
    pub type8: [zircon::sys::zx_koid_t; UNKNOWN as usize],
    pub type9: [zx_info_thread; 1 as usize],
    pub type10: [zx_exception_report; 1 as usize],
    pub type11: [zx_info_task_stats; 1 as usize],
    pub type12: [zx_info_maps; UNKNOWN as usize],
    pub type13: [zx_info_vmo; UNKNOWN as usize],
    pub type14: [zx_info_thread_stats; 1 as usize],
    pub type15: [zx_info_cpu_stats; UNKNOWN as usize],
    pub type16: [zx_info_kmem_stats; 1 as usize],
    pub type17: [zx_info_resource; 1 as usize],
    pub type18: [zx_info_handle_count; 1 as usize],
    pub type19: [zx_info_bti; 1 as usize],
    pub type20: [zx_info_process_handle_stats; 1 as usize],
    pub type21: [zx_info_socket; 1 as usize],
    pub type22: [zx_info_vmo; 1 as usize],
    pub type23: [zx_info_job; 1 as usize],
}

#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_object_info_handles {
    pub type1: zircon::sys::zx_handle_t,
    pub type2: zircon::sys::zx_handle_t,
    pub type3: zircon::sys::zx_handle_t,
    pub type4: zircon::sys::zx_handle_t,
    pub type5: zircon::sys::zx_handle_t,
    pub type6: zircon::sys::zx_handle_t,
    pub type7: zircon::sys::zx_handle_t,
    pub type8: zircon::sys::zx_handle_t,
    pub type9: zircon::sys::zx_handle_t,
    pub type10: zircon::sys::zx_handle_t,
    pub type11: zircon::sys::zx_handle_t,
    pub type12: zircon::sys::zx_handle_t,
    pub type13: zircon::sys::zx_handle_t,
    pub type14: zircon::sys::zx_handle_t,
    pub type15: zircon::sys::zx_handle_t,
    pub type16: zircon::sys::zx_handle_t,
    pub type17: zircon::sys::zx_handle_t,
    pub type18: zircon::sys::zx_handle_t,
    pub type19: zircon::sys::zx_handle_t,
    pub type20: zircon::sys::zx_handle_t,
    pub type21: zircon::sys::zx_handle_t,
    pub type22: zircon::sys::zx_handle_t,
    pub type23: zircon::sys::zx_handle_t,
}

#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_job_policy_types {
    pub type1: [zx_policy_basic; 1 as usize],
    pub type2: [zx_policy_timer_slack; 1 as usize],
}

#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_packet_user {
    pub u64: [u64; 4 as usize],
    pub u32: [u32; 8 as usize],
    pub u16: [u16; 16 as usize],
    pub c8: [i8; 32 as usize],
}

#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_iommu_types {
    pub type1: [zx_iommu_desc_dummy; 1 as usize],
    pub type2: [zx_iommu_desc_intel; 1 as usize],
}

#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_vcpu_read_state_types {
    pub type1: [zx_vcpu_state; 1 as usize],
}

#[repr(C)]
#[derive(Debug, Default, PartialEq)]
pub union zx_vcpu_write_state_types {
    pub type1: [zx_vcpu_state; 1 as usize],
    pub type2: [zx_vcpu_io; 1 as usize],
}

