// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.syscalls banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;



#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_handle_basic {
    pub koid: zircon::sys::zx_koid_t,
    pub rights: zircon::sys::zx_rights_t,
    pub type: u32,
    pub related_koid: zircon::sys::zx_koid_t,
    pub props: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_handle_count {
    pub handle_count: u32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zx_info_process_handle_stats {
    pub handle_count: [u32; 64 as usize],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_process {
    pub return_code: i64,
    pub started: bool,
    pub exited: bool,
    pub debugger_attached: bool,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_job {
    pub return_code: i64,
    pub exited: bool,
    pub kill_on_oom: bool,
    pub debugger_attached: bool,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_thread {
    pub state: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_thread_stats {
    pub total_runtime: zircon::sys::zx_duration_t,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_task_stats {
    pub mem_mapped_bytes: usize,
    pub mem_private_bytes: usize,
    pub mem_shared_bytes: usize,
    pub mem_scaled_shared_bytes: usize,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_vmar {
    pub base: usize,
    pub len: usize,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_bti {
    pub minimum_contiguity: u64,
    pub aspace_size: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_socket {
    pub options: u32,
    pub rx_buf_max: usize,
    pub rx_buf_size: usize,
    pub rx_buf_available: usize,
    pub tx_buf_max: usize,
    pub tx_buf_size: usize,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_maps_mapping {
    pub mmu_flags: zircon::sys::zx_vm_option_t,
    pub vmo_koid: zircon::sys::zx_koid_t,
    pub vmo_offset: u64,
    pub committed_pages: usize,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_maps {
    pub name: [i8; 32 as usize],
    pub base: zircon::sys::zx_vaddr_t,
    pub size: usize,
    pub depth: usize,
    pub type: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
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
#[derive(Copy, Clone, Debug, PartialEq)]
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
#[derive(Copy, Clone, Debug, PartialEq)]
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
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_info_resource {
    pub kind: u32,
    pub flags: u32,
    pub base: u64,
    pub size: usize,
    pub name: [i8; 32 as usize],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_wait_item {
    pub handle: zircon::sys::zx_handle_t,
    pub waitfor: zircon::sys::zx_signals_t,
    pub pending: zircon::sys::zx_signals_t,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_exception_header {
    pub size: u32,
    pub type: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_x86_64_exc_data {
    pub vector: u64,
    pub err_code: u64,
    pub cr2: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_arm64_exc_data {
    pub esr: u32,
    pub far: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_exception_context {

}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_exception_report {
    pub header: zx_exception_header,
    pub context: zx_exception_context,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_policy_basic {
    pub condition: u32,
    pub policy: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_policy_timer_slack {
    pub min_slack: zircon::sys::zx_duration_t,
    pub default_mode: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_packet_signal {
    pub trigger: zircon::sys::zx_signals_t,
    pub observed: zircon::sys::zx_signals_t,
    pub count: u64,
    pub reserved0: u64,
    pub reserved1: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_packet_exception {
    pub pid: u64,
    pub tid: u64,
    pub reserved0: u64,
    pub reserved1: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_packet_guest_bell {
    pub addr: zircon::sys::zx_gpaddr_t,
    pub reserved0: u64,
    pub reserved1: u64,
    pub reserved2: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_packet_guest_mem {
    pub addr: zircon::sys::zx_gpaddr_t,
    pub inst_len: u8,
    pub inst_buf: [i8; 15 as usize],
    pub default_operand_size: u8,
    pub reserved: [u8; 7 as usize],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_packet_guest_io {
    pub port: u16,
    pub access_size: u8,
    pub input: bool,
    pub reserved0: u64,
    pub reserved1: u64,
    pub reserved2: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_packet_guest_vcpu {
    pub type: u8,
    pub reserved: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_packet_interrupt {
    pub timestamp: zircon::sys::zx_time_t,
    pub reserved0: u64,
    pub reserved1: u64,
    pub reserved2: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_packet_page_request {
    pub command: u16,
    pub flags: u16,
    pub reserved0: u32,
    pub offset: u64,
    pub length: u64,
    pub reserved1: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_port_packet {
    pub key: u64,
    pub type: u32,
    pub status: zircon::sys::zx_status_t,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_profile_scheduler {
    pub priority: i32,
    pub boost: u32,
    pub deboost: u32,
    pub quantum: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_profile_info {
    pub type: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_iommu_desc_dummy {
    pub reserved: u8,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_iommu_desc_intel {
    pub register_base: u64,
    pub pci_segment: u16,
    pub whole_segment: bool,
    pub scope_bytes: u8,
    pub reserved_memory_bytes: u16,
    pub _reserved: [u8; 2 as usize],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_pci_bar {
    pub id: u32,
    pub type: u32,
    pub size: usize,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
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
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_pci_init_arg {
    pub num_irqs: u32,
    pub addr_window_count: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
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
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_smc_result {
    pub arg0: u64,
    pub arg1: u64,
    pub arg2: u64,
    pub arg3: u64,
    pub arg6: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
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
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_vcpu_io {
    pub access_size: u8,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_system_powerctl_arg {

}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_clock_id(zircon::sys::zx_clock_t);

impl zx_clock_id {
    pub const ZX_CLOCK_MONOTONIC: Self = Self(0);
    pub const ZX_CLOCK_UTC: Self = Self(1);
    pub const ZX_CLOCK_THREAD: Self = Self(2);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_feature_kind(u32);

impl zx_feature_kind {
    pub const ZX_FEATURE_KIND_CPU: Self = Self(0);
    pub const ZX_FEATURE_KIND_HW_BREAKPOINT_COUNT: Self = Self(1);
    pub const ZX_FEATURE_KIND_HW_WATCHPOINT_COUNT: Self = Self(2);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_event_kind(u32);

impl zx_event_kind {
    pub const ZX_SYSTEM_EVENT_LOW_MEMORY: Self = Self(0);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_cache_flush_options(u32);

impl zx_cache_flush_options {
    pub const ZX_CACHE_FLUSH_DATA: Self = Self(1);
    pub const ZX_CACHE_FLUSH_INVALIDATE: Self = Self(2);
    pub const ZX_CACHE_FLUSH_INSN: Self = Self(4);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_rights(zircon::sys::zx_rights_t);

impl zx_rights {
    pub const ZX_RIGHT_NONE: Self = Self(0);
    pub const ZX_RIGHT_DUPLICATE: Self = Self(1);
    pub const ZX_RIGHT_TRANSFER: Self = Self(2);
    pub const ZX_RIGHT_READ: Self = Self(4);
    pub const ZX_RIGHT_WRITE: Self = Self(8);
    pub const ZX_RIGHT_EXECUTE: Self = Self(16);
    pub const ZX_RIGHT_MAP: Self = Self(32);
    pub const ZX_RIGHT_GET_PROPERTY: Self = Self(64);
    pub const ZX_RIGHT_SET_PROPERTY: Self = Self(128);
    pub const ZX_RIGHT_ENUMERATE: Self = Self(256);
    pub const ZX_RIGHT_DESTROY: Self = Self(512);
    pub const ZX_RIGHT_SET_POLICY: Self = Self(1024);
    pub const ZX_RIGHT_GET_POLICY: Self = Self(2048);
    pub const ZX_RIGHT_SIGNAL: Self = Self(4096);
    pub const ZX_RIGHT_SIGNAL_PEER: Self = Self(8192);
    pub const ZX_RIGHT_WAIT: Self = Self(16384);
    pub const ZX_RIGHT_INSPECT: Self = Self(32768);
    pub const ZX_RIGHT_MANAGE_JOB: Self = Self(65536);
    pub const ZX_RIGHT_MANAGE_PROCESS: Self = Self(131072);
    pub const ZX_RIGHT_MANAGE_THREAD: Self = Self(262144);
    pub const ZX_RIGHT_APPLY_PROFILE: Self = Self(524288);
    pub const ZX_RIGHT_SAME_RIGHTS: Self = Self(2147483648);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_object_wait_async_options(u32);

impl zx_object_wait_async_options {
    pub const ZX_WAIT_ASYNC_ONCE: Self = Self(0);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_object_property_values(u32);

impl zx_object_property_values {
    pub const ZX_PROP_NAME: Self = Self(3);
    pub const ZX_PROP_PROCESS_DEBUG_ADDR: Self = Self(5);
    pub const ZX_PROP_PROCESS_VDSO_BASE_ADDRESS: Self = Self(6);
    pub const ZX_PROP_SOCKET_RX_THRESHOLD: Self = Self(12);
    pub const ZX_PROP_SOCKET_TX_THRESHOLD: Self = Self(13);
    pub const ZX_PROP_JOB_KILL_ON_OOM: Self = Self(15);
    pub const ZX_PROP_EXCEPTION_STATE: Self = Self(16);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_object_info_topics(u32);

impl zx_object_info_topics {
    pub const ZX_INFO_NONE: Self = Self(0);
    pub const ZX_INFO_HANDLE_VALID: Self = Self(1);
    pub const ZX_INFO_HANDLE_BASIC: Self = Self(2);
    pub const ZX_INFO_PROCESS: Self = Self(3);
    pub const ZX_INFO_PROCESS_THREADS: Self = Self(4);
    pub const ZX_INFO_VMAR: Self = Self(7);
    pub const ZX_INFO_JOB_CHILDREN: Self = Self(8);
    pub const ZX_INFO_JOB_PROCESSES: Self = Self(9);
    pub const ZX_INFO_THREAD: Self = Self(10);
    pub const ZX_INFO_THREAD_EXCEPTION_REPORT: Self = Self(11);
    pub const ZX_INFO_TASK_STATS: Self = Self(12);
    pub const ZX_INFO_PROCESS_MAPS: Self = Self(13);
    pub const ZX_INFO_PROCESS_VMOS: Self = Self(14);
    pub const ZX_INFO_THREAD_STATS: Self = Self(15);
    pub const ZX_INFO_CPU_STATS: Self = Self(16);
    pub const ZX_INFO_KMEM_STATS: Self = Self(17);
    pub const ZX_INFO_RESOURCE: Self = Self(18);
    pub const ZX_INFO_HANDLE_COUNT: Self = Self(19);
    pub const ZX_INFO_BTI: Self = Self(20);
    pub const ZX_INFO_PROCESS_HANDLE_STATS: Self = Self(21);
    pub const ZX_INFO_SOCKET: Self = Self(22);
    pub const ZX_INFO_VMO: Self = Self(23);
    pub const ZX_INFO_JOB: Self = Self(24);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_obj_props(u32);

impl zx_obj_props {
    pub const ZX_OBJ_PROP_NONE: Self = Self(0);
    pub const ZX_OBJ_PROP_WAITABLE: Self = Self(1);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_obj_types(u32);

impl zx_obj_types {
    pub const ZX_OBJ_TYPE_NONE: Self = Self(0);
    pub const ZX_OBJ_TYPE_PROCESS: Self = Self(1);
    pub const ZX_OBJ_TYPE_THREAD: Self = Self(2);
    pub const ZX_OBJ_TYPE_VMO: Self = Self(3);
    pub const ZX_OBJ_TYPE_CHANNEL: Self = Self(4);
    pub const ZX_OBJ_TYPE_EVENT: Self = Self(5);
    pub const ZX_OBJ_TYPE_PORT: Self = Self(6);
    pub const ZX_OBJ_TYPE_INTERRUPT: Self = Self(9);
    pub const ZX_OBJ_TYPE_PCI_DEVICE: Self = Self(11);
    pub const ZX_OBJ_TYPE_LOG: Self = Self(12);
    pub const ZX_OBJ_TYPE_SOCKET: Self = Self(14);
    pub const ZX_OBJ_TYPE_RESOURCE: Self = Self(15);
    pub const ZX_OBJ_TYPE_EVENTPAIR: Self = Self(16);
    pub const ZX_OBJ_TYPE_JOB: Self = Self(17);
    pub const ZX_OBJ_TYPE_VMAR: Self = Self(18);
    pub const ZX_OBJ_TYPE_FIFO: Self = Self(19);
    pub const ZX_OBJ_TYPE_GUEST: Self = Self(20);
    pub const ZX_OBJ_TYPE_VCPU: Self = Self(21);
    pub const ZX_OBJ_TYPE_TIMER: Self = Self(22);
    pub const ZX_OBJ_TYPE_IOMMU: Self = Self(23);
    pub const ZX_OBJ_TYPE_BTI: Self = Self(24);
    pub const ZX_OBJ_TYPE_PROFILE: Self = Self(25);
    pub const ZX_OBJ_TYPE_PMT: Self = Self(26);
    pub const ZX_OBJ_TYPE_SUSPEND_TOKEN: Self = Self(27);
    pub const ZX_OBJ_TYPE_PAGER: Self = Self(28);
    pub const ZX_OBJ_TYPE_EXCEPTION: Self = Self(29);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_thread_state_values(u32);

impl zx_thread_state_values {
    pub const ZX_THREAD_STATE_NEW: Self = Self(0);
    pub const ZX_THREAD_STATE_RUNNING: Self = Self(1);
    pub const ZX_THREAD_STATE_SUSPENDED: Self = Self(2);
    pub const ZX_THREAD_STATE_BLOCKED: Self = Self(3);
    pub const ZX_THREAD_STATE_DYING: Self = Self(4);
    pub const ZX_THREAD_STATE_DEAD: Self = Self(5);
    pub const ZX_THREAD_STATE_BLOCKED_EXCEPTION: Self = Self(259);
    pub const ZX_THREAD_STATE_BLOCKED_SLEEPING: Self = Self(515);
    pub const ZX_THREAD_STATE_BLOCKED_FUTEX: Self = Self(771);
    pub const ZX_THREAD_STATE_BLOCKED_PORT: Self = Self(1027);
    pub const ZX_THREAD_STATE_BLOCKED_CHANNEL: Self = Self(1283);
    pub const ZX_THREAD_STATE_BLOCKED_WAIT_ONE: Self = Self(1539);
    pub const ZX_THREAD_STATE_BLOCKED_WAIT_MANY: Self = Self(1795);
    pub const ZX_THREAD_STATE_BLOCKED_INTERRUPT: Self = Self(2051);
    pub const ZX_THREAD_STATE_BLOCKED_PAGER: Self = Self(2307);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_exception_channel_types(u32);

impl zx_exception_channel_types {
    pub const ZX_EXCEPTION_CHANNEL_TYPE_NONE: Self = Self(0);
    pub const ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER: Self = Self(1);
    pub const ZX_EXCEPTION_CHANNEL_TYPE_THREAD: Self = Self(2);
    pub const ZX_EXCEPTION_CHANNEL_TYPE_PROCESS: Self = Self(3);
    pub const ZX_EXCEPTION_CHANNEL_TYPE_JOB: Self = Self(4);
    pub const ZX_EXCEPTION_CHANNEL_TYPE_JOB_DEBUGGER: Self = Self(5);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_exception_port_types(u32);

impl zx_exception_port_types {
    pub const ZX_EXCEPTION_PORT_TYPE_NONE: Self = Self(0);
    pub const ZX_EXCEPTION_PORT_TYPE_DEBUGGER: Self = Self(1);
    pub const ZX_EXCEPTION_PORT_TYPE_THREAD: Self = Self(2);
    pub const ZX_EXCEPTION_PORT_TYPE_PROCESS: Self = Self(3);
    pub const ZX_EXCEPTION_PORT_TYPE_JOB: Self = Self(4);
    pub const ZX_EXCEPTION_PORT_TYPE_JOB_DEBUGGER: Self = Self(5);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_info_maps_type_vals(u32);

impl zx_info_maps_type_vals {
    pub const ZX_INFO_MAPS_TYPE_NONE: Self = Self(0);
    pub const ZX_INFO_MAPS_TYPE_ASPACE: Self = Self(1);
    pub const ZX_INFO_MAPS_TYPE_VMAR: Self = Self(2);
    pub const ZX_INFO_MAPS_TYPE_MAPPING: Self = Self(3);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_cache_policy_options(u32);

impl zx_cache_policy_options {
    pub const ZX_CACHE_POLICY_CACHED: Self = Self(0);
    pub const ZX_CACHE_POLICY_UNCACHED: Self = Self(1);
    pub const ZX_CACHE_POLICY_UNCACHED_DEVICE: Self = Self(2);
    pub const ZX_CACHE_POLICY_WRITE_COMBINING: Self = Self(3);
    pub const ZX_CACHE_POLICY_MASK: Self = Self(3);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_info_vmo_type_vals(u32);

impl zx_info_vmo_type_vals {
    pub const ZX_INFO_VMO_TYPE_PAGED: Self = Self(1);
    pub const ZX_INFO_VMO_TYPE_PHYSICAL: Self = Self(0);
    pub const ZX_INFO_VMO_RESIZABLE: Self = Self(2);
    pub const ZX_INFO_VMO_IS_COW_CLONE: Self = Self(4);
    pub const ZX_INFO_VMO_VIA_HANDLE: Self = Self(8);
    pub const ZX_INFO_VMO_VIA_MAPPING: Self = Self(16);
    pub const ZX_INFO_VMO_PAGER_BACKED: Self = Self(32);
    pub const ZX_INFO_VMO_CONTIGUOUS: Self = Self(64);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_socket_create_options(u32);

impl zx_socket_create_options {
    pub const ZX_SOCKET_STREAM: Self = Self(0);
    pub const ZX_SOCKET_DATAGRAM: Self = Self(1);
    pub const ZX_SOCKET_HAS_CONTROL: Self = Self(2);
    pub const ZX_SOCKET_HAS_ACCEPT: Self = Self(4);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_socket_read_options(u32);

impl zx_socket_read_options {
    pub const ZX_SOCKET_CONTROL: Self = Self(4);
    pub const ZX_SOCKET_PEEK: Self = Self(8);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_socket_write_options(u32);

impl zx_socket_write_options {
    pub const ZX_SOCKET_CONTROL: Self = Self(4);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_socket_shutdown_options(u32);

impl zx_socket_shutdown_options {
    pub const ZX_SOCKET_SHUTDOWN_WRITE: Self = Self(1);
    pub const ZX_SOCKET_SHUTDOWN_READ: Self = Self(2);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct thread_state_kind(u32);

impl thread_state_kind {
    pub const ZX_THREAD_STATE_GENERAL_REGS: Self = Self(0);
    pub const ZX_THREAD_STATE_FP_REGS: Self = Self(1);
    pub const ZX_THREAD_STATE_VECTOR_REGS: Self = Self(2);
    pub const ZX_THREAD_STATE_DEBUG_REGS: Self = Self(4);
    pub const ZX_THREAD_STATE_SINGLE_STEP: Self = Self(5);
    pub const ZX_THREAD_X86_REGISTER_FS: Self = Self(6);
    pub const ZX_THREAD_X86_REGISTER_GS: Self = Self(7);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_job_policy_options(u32);

impl zx_job_policy_options {
    pub const ZX_JOB_POL_RELATIVE: Self = Self(0);
    pub const ZX_JOB_POL_ABSOLUTE: Self = Self(1);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_job_policy_topic(u32);

impl zx_job_policy_topic {
    pub const ZX_JOB_POL_BASIC: Self = Self(0);
    pub const ZX_JOB_POL_TIMER_SLACK: Self = Self(1);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_job_policy_conditions(u32);

impl zx_job_policy_conditions {
    pub const ZX_POL_BAD_HANDLE: Self = Self(0);
    pub const ZX_POL_WRONG_OBJECT: Self = Self(1);
    pub const ZX_POL_VMAR_WX: Self = Self(2);
    pub const ZX_POL_NEW_ANY: Self = Self(3);
    pub const ZX_POL_NEW_VMO: Self = Self(4);
    pub const ZX_POL_NEW_CHANNEL: Self = Self(5);
    pub const ZX_POL_NEW_EVENT: Self = Self(6);
    pub const ZX_POL_NEW_EVENTPAIR: Self = Self(7);
    pub const ZX_POL_NEW_PORT: Self = Self(8);
    pub const ZX_POL_NEW_SOCKET: Self = Self(9);
    pub const ZX_POL_NEW_FIFO: Self = Self(10);
    pub const ZX_POL_NEW_TIMER: Self = Self(11);
    pub const ZX_POL_NEW_PROCESS: Self = Self(12);
    pub const ZX_POL_NEW_PROFILE: Self = Self(13);
    pub const ZX_POL_AMBIENT_MARK_VMO_EXEC: Self = Self(14);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_job_policy_actions(u32);

impl zx_job_policy_actions {
    pub const ZX_POL_ACTION_ALLOW: Self = Self(0);
    pub const ZX_POL_ACTION_DENY: Self = Self(1);
    pub const ZX_POL_ACTION_ALLOW_EXCEPTION: Self = Self(2);
    pub const ZX_POL_ACTION_DENY_EXCEPTION: Self = Self(3);
    pub const ZX_POL_ACTION_KILL: Self = Self(4);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_task_exception_port_options(u32);

impl zx_task_exception_port_options {
    pub const ZX_EXCEPTION_PORT_DEBUGGER: Self = Self(1);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_task_resume_options(u32);

impl zx_task_resume_options {
    pub const ZX_RESUME_TRY_NEXT: Self = Self(2);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_port_create_options(u32);

impl zx_port_create_options {
    pub const ZX_PORT_BIND_TO_INTERRUPT: Self = Self(1);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_port_packet_type(u32);

impl zx_port_packet_type {
    pub const ZX_PKT_TYPE_USER: Self = Self(0);
    pub const ZX_PKT_TYPE_SIGNAL_ONE: Self = Self(1);
    pub const ZX_PKT_TYPE_SIGNAL_REP: Self = Self(2);
    pub const ZX_PKT_TYPE_GUEST_BELL: Self = Self(3);
    pub const ZX_PKT_TYPE_GUEST_MEM: Self = Self(4);
    pub const ZX_PKT_TYPE_GUEST_IO: Self = Self(5);
    pub const ZX_PKT_TYPE_GUEST_VCPU: Self = Self(6);
    pub const ZX_PKT_TYPE_INTERRUPT: Self = Self(7);
    pub const ZX_PKT_TYPE_PAGE_REQUEST: Self = Self(9);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_timer_options(u32);

impl zx_timer_options {
    pub const ZX_TIMER_SLACK_CENTER: Self = Self(0);
    pub const ZX_TIMER_SLACK_EARLY: Self = Self(1);
    pub const ZX_TIMER_SLACK_LATE: Self = Self(2);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_vmo_create_options(u32);

impl zx_vmo_create_options {
    pub const ZX_VMO_RESIZABLE: Self = Self(2);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_vmo_opcodes(u32);

impl zx_vmo_opcodes {
    pub const ZX_VMO_OP_COMMIT: Self = Self(1);
    pub const ZX_VMO_OP_DECOMMIT: Self = Self(2);
    pub const ZX_VMO_OP_LOCK: Self = Self(3);
    pub const ZX_VMO_OP_UNLOCK: Self = Self(4);
    pub const ZX_VMO_OP_CACHE_SYNC: Self = Self(6);
    pub const ZX_VMO_OP_CACHE_INVALIDATE: Self = Self(7);
    pub const ZX_VMO_OP_CACHE_CLEAN: Self = Self(8);
    pub const ZX_VMO_OP_CACHE_CLEAN_INVALIDATE: Self = Self(9);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_vmo_cache_policy_options(u32);

impl zx_vmo_cache_policy_options {
    pub const ZX_CACHE_POLICY_CACHED: Self = Self(0);
    pub const ZX_CACHE_POLICY_UNCACHED: Self = Self(1);
    pub const ZX_CACHE_POLICY_UNCACHED_DEVICE: Self = Self(2);
    pub const ZX_CACHE_POLICY_WRITE_COMBINING: Self = Self(3);
    pub const ZX_CACHE_POLICY_MASK: Self = Self(3);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_vmo_clone_options(u32);

impl zx_vmo_clone_options {
    pub const ZX_VMO_CLONE_COPY_ON_WRITE: Self = Self(1);
    pub const ZX_VMO_CHILD_COPY_ON_WRITE: Self = Self(1);
    pub const ZX_VMO_CHILD_RESIZABLE: Self = Self(4);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_vmar_allocate_map_options(zircon::sys::zx_vm_option_t);

impl zx_vmar_allocate_map_options {
    pub const ZX_VM_PERM_READ: Self = Self(1);
    pub const ZX_VM_PERM_WRITE: Self = Self(2);
    pub const ZX_VM_PERM_EXECUTE: Self = Self(4);
    pub const ZX_VM_COMPACT: Self = Self(8);
    pub const ZX_VM_SPECIFIC: Self = Self(16);
    pub const ZX_VM_SPECIFIC_OVERWRITE: Self = Self(32);
    pub const ZX_VM_CAN_MAP_SPECIFIC: Self = Self(64);
    pub const ZX_VM_CAN_MAP_READ: Self = Self(128);
    pub const ZX_VM_CAN_MAP_WRITE: Self = Self(256);
    pub const ZX_VM_CAN_MAP_EXECUTE: Self = Self(512);
    pub const ZX_VM_MAP_RANGE: Self = Self(1024);
    pub const ZX_VM_REQUIRE_NON_RESIZABLE: Self = Self(2048);
    pub const ZX_VM_ALLOW_FAULTS: Self = Self(4096);
    pub const ZX_VM_ALIGN_1KB: Self = Self(167772160);
    pub const ZX_VM_ALIGN_2KB: Self = Self(184549376);
    pub const ZX_VM_ALIGN_4KB: Self = Self(201326592);
    pub const ZX_VM_ALIGN_8KB: Self = Self(218103808);
    pub const ZX_VM_ALIGN_16KB: Self = Self(234881024);
    pub const ZX_VM_ALIGN_32KB: Self = Self(251658240);
    pub const ZX_VM_ALIGN_64KB: Self = Self(268435456);
    pub const ZX_VM_ALIGN_128KB: Self = Self(285212672);
    pub const ZX_VM_ALIGN_256KB: Self = Self(301989888);
    pub const ZX_VM_ALIGN_512KB: Self = Self(318767104);
    pub const ZX_VM_ALIGN_1MB: Self = Self(335544320);
    pub const ZX_VM_ALIGN_2MB: Self = Self(352321536);
    pub const ZX_VM_ALIGN_4MB: Self = Self(369098752);
    pub const ZX_VM_ALIGN_8MB: Self = Self(385875968);
    pub const ZX_VM_ALIGN_16MB: Self = Self(402653184);
    pub const ZX_VM_ALIGN_32MB: Self = Self(419430400);
    pub const ZX_VM_ALIGN_64MB: Self = Self(436207616);
    pub const ZX_VM_ALIGN_128MB: Self = Self(452984832);
    pub const ZX_VM_ALIGN_256MB: Self = Self(469762048);
    pub const ZX_VM_ALIGN_512MB: Self = Self(486539264);
    pub const ZX_VM_ALIGN_1GB: Self = Self(503316480);
    pub const ZX_VM_ALIGN_2GB: Self = Self(520093696);
    pub const ZX_VM_ALIGN_4GB: Self = Self(536870912);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_vmar_protect_options(zircon::sys::zx_vm_option_t);

impl zx_vmar_protect_options {
    pub const ZX_VM_PERM_READ: Self = Self(1);
    pub const ZX_VM_PERM_WRITE: Self = Self(2);
    pub const ZX_VM_PERM_EXECUTE: Self = Self(4);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_profile_scheduler_priority(i32);

impl zx_profile_scheduler_priority {
    pub const ZX_PRIORITY_LOWEST: Self = Self(0);
    pub const ZX_PRIORITY_LOW: Self = Self(8);
    pub const ZX_PRIORITY_DEFAULT: Self = Self(16);
    pub const ZX_PRIORITY_HIGH: Self = Self(24);
    pub const ZX_PRIORITY_HIGHEST: Self = Self(31);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_profile_info_type(u32);

impl zx_profile_info_type {
    pub const ZX_PROFILE_INFO_SCHEDULER: Self = Self(1);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_debuglog_create_options(u32);

impl zx_debuglog_create_options {
    pub const ZX_LOG_FLAG_READABLE: Self = Self(1073741824);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_interrupt_options(u32);

impl zx_interrupt_options {
    pub const ZX_INTERRUPT_REMAP_IRQ: Self = Self(1);
    pub const ZX_INTERRUPT_MODE_DEFAULT: Self = Self(0);
    pub const ZX_INTERRUPT_MODE_EDGE_LOW: Self = Self(2);
    pub const ZX_INTERRUPT_MODE_EDGE_HIGH: Self = Self(4);
    pub const ZX_INTERRUPT_MODE_LEVEL_LOW: Self = Self(6);
    pub const ZX_INTERRUPT_MODE_LEVEL_HIGH: Self = Self(8);
    pub const ZX_INTERRUPT_MODE_EDGE_BOTH: Self = Self(10);
    pub const ZX_INTERRUPT_MODE_MASK: Self = Self(14);
    pub const ZX_INTERRUPT_VIRTUAL: Self = Self(16);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_iommu_type(u32);

impl zx_iommu_type {
    pub const ZX_IOMMU_TYPE_DUMMY: Self = Self(0);
    pub const ZX_IOMMU_TYPE_INTEL: Self = Self(1);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_bti_pin_options(u32);

impl zx_bti_pin_options {
    pub const ZX_BTI_PERM_READ: Self = Self(1);
    pub const ZX_BTI_PERM_WRITE: Self = Self(2);
    pub const ZX_BTI_PERM_EXECUTE: Self = Self(4);
    pub const ZX_BTI_COMPRESS: Self = Self(8);
    pub const ZX_BTI_CONTIGUOUS: Self = Self(16);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_rsrc_kind_vals(u32);

impl zx_rsrc_kind_vals {
    pub const ZX_RSRC_KIND_MMIO: Self = Self(0);
    pub const ZX_RSRC_KIND_IRQ: Self = Self(1);
    pub const ZX_RSRC_KIND_IOPORT: Self = Self(2);
    pub const ZX_RSRC_KIND_HYPERVISOR: Self = Self(3);
    pub const ZX_RSRC_KIND_ROOT: Self = Self(4);
    pub const ZX_RSRC_KIND_VMEX: Self = Self(5);
    pub const ZX_RSRC_KIND_SMC: Self = Self(6);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_guest_trap_kind(u32);

impl zx_guest_trap_kind {
    pub const ZX_GUEST_TRAP_BELL: Self = Self(0);
    pub const ZX_GUEST_TRAP_MEM: Self = Self(1);
    pub const ZX_GUEST_TRAP_IO: Self = Self(2);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_vcpu_read_state_kind(u32);

impl zx_vcpu_read_state_kind {
    pub const ZX_VCPU_STATE: Self = Self(0);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_vcpu_write_state_kind(u32);

impl zx_vcpu_write_state_kind {
    pub const ZX_VCPU_STATE: Self = Self(0);
    pub const ZX_VCPU_IO: Self = Self(1);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_system_powerctl_cmd(u32);

impl zx_system_powerctl_cmd {
    pub const ZX_SYSTEM_POWERCTL_ENABLE_ALL_CPUS: Self = Self(1);
    pub const ZX_SYSTEM_POWERCTL_DISABLE_ALL_CPUS_BUT_PRIMARY: Self = Self(2);
    pub const ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE: Self = Self(3);
    pub const ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1: Self = Self(4);
    pub const ZX_SYSTEM_POWERCTL_REBOOT: Self = Self(5);
    pub const ZX_SYSTEM_POWERCTL_REBOOT_BOOTLOADER: Self = Self(6);
    pub const ZX_SYSTEM_POWERCTL_REBOOT_RECOVERY: Self = Self(7);
    pub const ZX_SYSTEM_POWERCTL_SHUTDOWN: Self = Self(8);
}


#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_object_property_handles {
    pub type1: zircon::sys::zx_handle_t,
    pub type2: zircon::sys::zx_handle_t,
    pub type3: zircon::sys::zx_handle_t,
    pub type4: zircon::sys::zx_handle_t,
    pub type5: zircon::sys::zx_handle_t,
    pub type6: zircon::sys::zx_handle_t,
    pub type7: zircon::sys::zx_handle_t,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_object_property_handles {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_object_property_handles>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
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

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_object_info_types {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_object_info_types>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
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

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_object_info_handles {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_object_info_handles>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_job_policy_types {
    pub type1: [zx_policy_basic; 1 as usize],
    pub type2: [zx_policy_timer_slack; 1 as usize],
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_job_policy_types {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_job_policy_types>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_packet_user {
    pub u64: [u64; 4 as usize],
    pub u32: [u32; 8 as usize],
    pub u16: [u16; 16 as usize],
    pub c8: [i8; 32 as usize],
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_packet_user {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_packet_user>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_iommu_types {
    pub type1: [zx_iommu_desc_dummy; 1 as usize],
    pub type2: [zx_iommu_desc_intel; 1 as usize],
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_iommu_types {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_iommu_types>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_vcpu_read_state_types {
    pub type1: [zx_vcpu_state; 1 as usize],
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_vcpu_read_state_types {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_vcpu_read_state_types>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_vcpu_write_state_types {
    pub type1: [zx_vcpu_state; 1 as usize],
    pub type2: [zx_vcpu_io; 1 as usize],
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_vcpu_write_state_types {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_vcpu_write_state_types>")
    }
}

