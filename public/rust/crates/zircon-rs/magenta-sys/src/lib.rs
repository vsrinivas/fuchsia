// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

extern crate core;

#[macro_use]
extern crate bitflags;

pub type mx_handle_t = i32;

pub type mx_status_t = i32;

pub type mx_futex_t = isize;
pub type mx_paddr_t = usize;

// Auto-generated using tools/gen_status.py
pub const MX_OK                    : mx_status_t = 0;
pub const MX_ERR_INTERNAL          : mx_status_t = -1;
pub const MX_ERR_NOT_SUPPORTED     : mx_status_t = -2;
pub const MX_ERR_NO_RESOURCES      : mx_status_t = -3;
pub const MX_ERR_NO_MEMORY         : mx_status_t = -4;
pub const MX_ERR_CALL_FAILED       : mx_status_t = -5;
pub const MX_ERR_INTERRUPTED_RETRY : mx_status_t = -6;
pub const MX_ERR_INVALID_ARGS      : mx_status_t = -10;
pub const MX_ERR_BAD_HANDLE        : mx_status_t = -11;
pub const MX_ERR_WRONG_TYPE        : mx_status_t = -12;
pub const MX_ERR_BAD_SYSCALL       : mx_status_t = -13;
pub const MX_ERR_OUT_OF_RANGE      : mx_status_t = -14;
pub const MX_ERR_BUFFER_TOO_SMALL  : mx_status_t = -15;
pub const MX_ERR_BAD_STATE         : mx_status_t = -20;
pub const MX_ERR_TIMED_OUT         : mx_status_t = -21;
pub const MX_ERR_SHOULD_WAIT       : mx_status_t = -22;
pub const MX_ERR_CANCELED          : mx_status_t = -23;
pub const MX_ERR_PEER_CLOSED       : mx_status_t = -24;
pub const MX_ERR_NOT_FOUND         : mx_status_t = -25;
pub const MX_ERR_ALREADY_EXISTS    : mx_status_t = -26;
pub const MX_ERR_ALREADY_BOUND     : mx_status_t = -27;
pub const MX_ERR_UNAVAILABLE       : mx_status_t = -28;
pub const MX_ERR_ACCESS_DENIED     : mx_status_t = -30;
pub const MX_ERR_IO                : mx_status_t = -40;
pub const MX_ERR_IO_REFUSED        : mx_status_t = -41;
pub const MX_ERR_IO_DATA_INTEGRITY : mx_status_t = -42;
pub const MX_ERR_IO_DATA_LOSS      : mx_status_t = -43;
pub const MX_ERR_BAD_PATH          : mx_status_t = -50;
pub const MX_ERR_NOT_DIR           : mx_status_t = -51;
pub const MX_ERR_NOT_FILE          : mx_status_t = -52;
pub const MX_ERR_FILE_BIG          : mx_status_t = -53;
pub const MX_ERR_NO_SPACE          : mx_status_t = -54;
pub const MX_ERR_STOP              : mx_status_t = -60;
pub const MX_ERR_NEXT              : mx_status_t = -61;

pub type mx_time_t = u64;
pub type mx_duration_t = u64;
pub const MX_TIME_INFINITE : mx_time_t = core::u64::MAX;

bitflags! {
    #[repr(C)]
    pub flags mx_signals_t: u32 {
        const MX_SIGNAL_NONE              = 0,
        const MX_OBJECT_SIGNAL_ALL        = 0x00ffffff,
        const MX_USER_SIGNAL_ALL          = 0xff000000,
        const MX_OBJECT_SIGNAL_0          = 1 << 0,
        const MX_OBJECT_SIGNAL_1          = 1 << 1,
        const MX_OBJECT_SIGNAL_2          = 1 << 2,
        const MX_OBJECT_SIGNAL_3          = 1 << 3,
        const MX_OBJECT_SIGNAL_4          = 1 << 4,
        const MX_OBJECT_SIGNAL_5          = 1 << 5,
        const MX_OBJECT_SIGNAL_6          = 1 << 6,
        const MX_OBJECT_SIGNAL_7          = 1 << 7,
        const MX_OBJECT_SIGNAL_8          = 1 << 8,
        const MX_OBJECT_SIGNAL_9          = 1 << 9,
        const MX_OBJECT_SIGNAL_10         = 1 << 10,
        const MX_OBJECT_SIGNAL_11         = 1 << 11,
        const MX_OBJECT_SIGNAL_12         = 1 << 12,
        const MX_OBJECT_SIGNAL_13         = 1 << 13,
        const MX_OBJECT_SIGNAL_14         = 1 << 14,
        const MX_OBJECT_SIGNAL_15         = 1 << 15,
        const MX_OBJECT_SIGNAL_16         = 1 << 16,
        const MX_OBJECT_SIGNAL_17         = 1 << 17,
        const MX_OBJECT_SIGNAL_18         = 1 << 18,
        const MX_OBJECT_SIGNAL_19         = 1 << 19,
        const MX_OBJECT_SIGNAL_20         = 1 << 20,
        const MX_OBJECT_SIGNAL_21         = 1 << 21,
        const MX_OBJECT_LAST_HANDLE       = 1 << 22,
        const MX_OBJECT_HANDLE_CLOSED     = 1 << 23,
        const MX_USER_SIGNAL_0            = 1 << 24,
        const MX_USER_SIGNAL_1            = 1 << 25,
        const MX_USER_SIGNAL_2            = 1 << 26,
        const MX_USER_SIGNAL_3            = 1 << 27,
        const MX_USER_SIGNAL_4            = 1 << 28,
        const MX_USER_SIGNAL_5            = 1 << 29,
        const MX_USER_SIGNAL_6            = 1 << 30,
        const MX_USER_SIGNAL_7            = 1 << 31,

        const MX_OBJECT_READABLE          = MX_OBJECT_SIGNAL_0.bits,
        const MX_OBJECT_WRITABLE          = MX_OBJECT_SIGNAL_1.bits,
        const MX_OBJECT_PEER_CLOSED       = MX_OBJECT_SIGNAL_2.bits,

        // Cancelation (handle was closed while waiting with it)
        const MX_SIGNAL_HANDLE_CLOSED     = MX_OBJECT_HANDLE_CLOSED.bits,

        // Only one user-more reference (handle) to the object exists.
        const MX_SIGNAL_LAST_HANDLE       = MX_OBJECT_LAST_HANDLE.bits,

        // Event
        const MX_EVENT_SIGNALED           = MX_OBJECT_SIGNAL_3.bits,

        // EventPair
        const MX_EPAIR_SIGNALED           = MX_OBJECT_SIGNAL_3.bits,
        const MX_EPAIR_CLOSED             = MX_OBJECT_SIGNAL_2.bits,

        // Task signals (process, thread, job)
        const MX_TASK_TERMINATED          = MX_OBJECT_SIGNAL_3.bits,

        // Channel
        const MX_CHANNEL_READABLE         = MX_OBJECT_SIGNAL_0.bits,
        const MX_CHANNEL_WRITABLE         = MX_OBJECT_SIGNAL_1.bits,
        const MX_CHANNEL_PEER_CLOSED      = MX_OBJECT_SIGNAL_2.bits,

        // Socket
        const MX_SOCKET_READABLE          = MX_OBJECT_SIGNAL_0.bits,
        const MX_SOCKET_WRITABLE          = MX_OBJECT_SIGNAL_1.bits,
        const MX_SOCKET_PEER_CLOSED       = MX_OBJECT_SIGNAL_2.bits,

        // Port
        const MX_PORT_READABLE            = MX_OBJECT_READABLE.bits,

        // Resource
        const MX_RESOURCE_DESTROYED       = MX_OBJECT_SIGNAL_3.bits,
        const MX_RESOURCE_READABLE        = MX_OBJECT_READABLE.bits,
        const MX_RESOURCE_WRITABLE        = MX_OBJECT_WRITABLE.bits,
        const MX_RESOURCE_CHILD_ADDED     = MX_OBJECT_SIGNAL_4.bits,

        // Fifo
        const MX_FIFO_READABLE            = MX_OBJECT_READABLE.bits,
        const MX_FIFO_WRITABLE            = MX_OBJECT_WRITABLE.bits,
        const MX_FIFO_PEER_CLOSED         = MX_OBJECT_PEER_CLOSED.bits,

        // Job
        const MX_JOB_NO_PROCESSES         = MX_OBJECT_SIGNAL_3.bits,
        const MX_JOB_NO_JOBS              = MX_OBJECT_SIGNAL_4.bits,

        // Process
        const MX_PROCESS_TERMINATED       = MX_OBJECT_SIGNAL_3.bits,

        // Thread
        const MX_THREAD_TERMINATED        = MX_OBJECT_SIGNAL_3.bits,

        // Log
        const MX_LOG_READABLE             = MX_OBJECT_READABLE.bits,
        const MX_LOG_WRITABLE             = MX_OBJECT_WRITABLE.bits,

        // Timer
        const MX_TIMER_SIGNALED           = MX_OBJECT_SIGNAL_3.bits,
    }
}

pub type mx_size_t = usize;
pub type mx_ssize_t = isize;

bitflags! {
    #[repr(C)]
    pub flags mx_rights_t: u32 {
        const MX_RIGHT_NONE         = 0,
        const MX_RIGHT_DUPLICATE    = 1 << 0,
        const MX_RIGHT_TRANSFER     = 1 << 1,
        const MX_RIGHT_READ         = 1 << 2,
        const MX_RIGHT_WRITE        = 1 << 3,
        const MX_RIGHT_EXECUTE      = 1 << 4,
        const MX_RIGHT_MAP          = 1 << 5,
        const MX_RIGHT_GET_PROPERTY = 1 << 6,
        const MX_RIGHT_SET_PROPERTY = 1 << 7,
        const MX_RIGHT_DEBUG        = 1 << 8,
        const MX_RIGHT_SAME_RIGHTS  = 1 << 31,
    }
}

// clock ids
pub const MX_CLOCK_MONOTONIC: u32 = 0;

// Buffer size limits on the cprng syscalls
pub const MX_CPRNG_DRAW_MAX_LEN: usize = 256;
pub const MX_CPRNG_ADD_ENTROPY_MAX_LEN: usize = 256;

// Socket flags and limits.
pub const MX_SOCKET_HALF_CLOSE: u32 = 1;

// VM Object opcodes
pub const MX_VMO_OP_COMMIT: u32 = 1;
pub const MX_VMO_OP_DECOMMIT: u32 = 2;
pub const MX_VMO_OP_LOCK: u32 = 3;
pub const MX_VMO_OP_UNLOCK: u32 = 4;
pub const MX_VMO_OP_LOOKUP: u32 = 5;
pub const MX_VMO_OP_CACHE_SYNC: u32 = 6;
pub const MX_VMO_OP_CACHE_INVALIDATE: u32 = 7;
pub const MX_VMO_OP_CACHE_CLEAN: u32 = 8;
pub const MX_VMO_OP_CACHE_CLEAN_INVALIDATE: u32 = 9;

// VM Object clone flags
pub const MX_VMO_CLONE_COPY_ON_WRITE: u32 = 1;

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum mx_cache_policy_t {
    MX_CACHE_POLICY_CACHED = 0,
    MX_CACHE_POLICY_UNCACHED = 1,
    MX_CACHE_POLICY_UNCACHED_DEVICE = 2,
    MX_CACHE_POLICY_WRITE_COMBINING = 3,
}

#[repr(C)]
pub struct mx_wait_item_t {
    pub handle: mx_handle_t,
    pub waitfor: mx_signals_t,
    pub pending: mx_signals_t,
}

#[repr(C)]
pub struct mx_waitset_result_t {
    pub cookie: u64,
    pub status: mx_status_t,
    pub observed: mx_signals_t,
}

#[repr(C)]
pub struct mx_channel_call_args_t {
    pub wr_bytes: *const u8,
    pub wr_handles: *const mx_handle_t,
    pub rd_bytes: *mut u8,
    pub rd_handles: *mut mx_handle_t,
    pub wr_num_bytes: u32,
    pub wr_num_handles: u32,
    pub rd_num_bytes: u32,
    pub rd_num_handles: u32,
}

pub type mx_pci_irq_swizzle_lut_t = [[[u32; 4]; 8]; 32];

#[repr(C)]
pub struct mx_pci_init_arg_t {
    pub dev_pin_to_global_irq: mx_pci_irq_swizzle_lut_t,
    pub num_irqs: u32,
    pub irqs: [mx_irq_t; 32],
    pub ecam_window_count: u32,
    // Note: the ecam_windows field is actually a variable size array.
    // We use a fixed size array to match the C repr.
    pub ecam_windows: [mx_ecam_window_t; 1],
}

#[repr(C)]
pub struct mx_irq_t {
    pub global_irq: u32,
    pub level_triggered: bool,
    pub active_high: bool,
}

#[repr(C)]
pub struct mx_ecam_window_t {
    pub base: u64,
    pub size: usize,
    pub bus_start: u8,
    pub bus_end: u8,
}

#[repr(C)]
pub struct mx_pcie_device_info_t {
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
pub struct mx_pci_resource_t {
    pub type_: u32,
    pub size: usize,
    // TODO: Actually a union
    pub pio_addr: usize,
}

// TODO: Actually a union
pub type mx_rrec_t = [u8; 64];

// Ports V2
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum mx_packet_type_t {
    MX_PKT_TYPE_USER = 0,
    MX_PKT_TYPE_SIGNAL_ONE = 1,
    MX_PKT_TYPE_SIGNAL_REP = 2,
}

impl Default for mx_packet_type_t {
    fn default() -> Self {
        mx_packet_type_t::MX_PKT_TYPE_USER
    }
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct mx_packet_signal_t {
    pub trigger: mx_signals_t,
    pub observed: mx_signals_t,
    pub count: u64,
}

pub const MX_WAIT_ASYNC_ONCE: u32 = 0;
pub const MX_WAIT_ASYNC_REPEATING: u32 = 1;

// Actually a union of different integer types, but this should be good enough.
pub type mx_packet_user_t = [u8; 32];

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct mx_port_packet_t {
    pub key: u64,
    pub packet_type: mx_packet_type_t,
    pub status: i32,
    pub union: [u8; 32],
}

include!("definitions.rs");
