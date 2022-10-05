// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

use static_assertions::const_assert_eq;
use std::fmt::{self, Debug};
use std::hash::{Hash, Hasher};
use std::sync::atomic::AtomicI32;

pub type zx_addr_t = usize;
pub type zx_stream_seek_origin_t = u32;
pub type zx_clock_t = u32;
pub type zx_duration_t = i64;
pub type zx_futex_t = AtomicI32;
pub type zx_gpaddr_t = usize;
pub type zx_guest_option_t = u32;
pub type zx_vcpu_option_t = u32;
pub type zx_guest_trap_t = u32;
pub type zx_handle_t = u32;
pub type zx_handle_op_t = u32;
pub type zx_koid_t = u64;
pub type zx_obj_type_t = u32;
pub type zx_object_info_topic_t = u32;
pub type zx_info_maps_type_t = u32;
pub type zx_off_t = u64;
pub type zx_paddr_t = usize;
pub type zx_rights_t = u32;
pub type zx_rsrc_flags_t = u32;
pub type zx_rsrc_kind_t = u32;
pub type zx_signals_t = u32;
pub type zx_ssize_t = isize;
pub type zx_status_t = i32;
pub type zx_rsrc_system_base_t = u64;
pub type zx_ticks_t = i64;
pub type zx_time_t = i64;
pub type zx_vaddr_t = usize;
pub type zx_vm_option_t = u32;
pub type zx_thread_state_topic_t = u32;
pub type zx_vcpu_state_topic_t = u32;

// TODO: magically coerce this to &`static str somehow?
#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_string_view_t {
    pub c_str: *const u8, // Guaranteed NUL-terminated valid UTF-8.
    pub length: usize,
}

pub const ZX_MAX_NAME_LEN: usize = 32;

// TODO: combine these macros with the bitflags and assoc consts macros below
// so that we only have to do one macro invocation.
// The result would look something like:
// multiconst!(bitflags, zx_rights_t, Rights, [RIGHT_NONE => ZX_RIGHT_NONE = 0; ...]);
// multiconst!(assoc_consts, zx_status_t, Status, [OK => ZX_OK = 0; ...]);
// Note that the actual name of the inner macro (e.g. `bitflags`) can't be a variable.
// It'll just have to be matched on manually
macro_rules! multiconst {
    ($typename:ident, [$($(#[$attr:meta])* $rawname:ident = $value:expr;)*]) => {
        $(
            $(#[$attr])*
            pub const $rawname: $typename = $value;
        )*
    }
}

multiconst!(zx_handle_t, [
    ZX_HANDLE_INVALID = 0;
]);

multiconst!(zx_handle_op_t, [
    ZX_HANDLE_OP_MOVE = 0;
    ZX_HANDLE_OP_DUPLICATE = 1;
]);

multiconst!(zx_koid_t, [
    ZX_KOID_INVALID = 0;
    ZX_KOID_KERNEL = 1;
    ZX_KOID_FIRST = 1024;
]);

multiconst!(zx_time_t, [
    ZX_TIME_INFINITE = ::std::i64::MAX;
    ZX_TIME_INFINITE_PAST = ::std::i64::MIN;
]);

multiconst!(zx_rights_t, [
    ZX_RIGHT_NONE           = 0;
    ZX_RIGHT_DUPLICATE      = 1 << 0;
    ZX_RIGHT_TRANSFER       = 1 << 1;
    ZX_RIGHT_READ           = 1 << 2;
    ZX_RIGHT_WRITE          = 1 << 3;
    ZX_RIGHT_EXECUTE        = 1 << 4;
    ZX_RIGHT_MAP            = 1 << 5;
    ZX_RIGHT_GET_PROPERTY   = 1 << 6;
    ZX_RIGHT_SET_PROPERTY   = 1 << 7;
    ZX_RIGHT_ENUMERATE      = 1 << 8;
    ZX_RIGHT_DESTROY        = 1 << 9;
    ZX_RIGHT_SET_POLICY     = 1 << 10;
    ZX_RIGHT_GET_POLICY     = 1 << 11;
    ZX_RIGHT_SIGNAL         = 1 << 12;
    ZX_RIGHT_SIGNAL_PEER    = 1 << 13;
    ZX_RIGHT_WAIT           = 1 << 14;
    ZX_RIGHT_INSPECT        = 1 << 15;
    ZX_RIGHT_MANAGE_JOB     = 1 << 16;
    ZX_RIGHT_MANAGE_PROCESS = 1 << 17;
    ZX_RIGHT_MANAGE_THREAD  = 1 << 18;
    ZX_RIGHT_APPLY_PROFILE  = 1 << 19;
    ZX_RIGHT_MANAGE_SOCKET  = 1 << 20;
    ZX_RIGHT_SAME_RIGHTS    = 1 << 31;
]);

multiconst!(u32, [
    ZX_VMO_RESIZABLE = 1 << 1;
    ZX_VMO_DISCARDABLE = 1 << 2;
    ZX_VMO_TRAP_DIRTY = 1 << 3;
]);

multiconst!(u32, [
    ZX_INFO_VMO_RESIZABLE = 1 << 1;
    ZX_INFO_VMO_IS_COW_CLONE = 1 << 2;
    ZX_INFO_VMO_PAGER_BACKED = 1 << 5;
    ZX_INFO_VMO_CONTIGUOUS = 1 << 6;
]);

// TODO: add an alias for this type in the C headers.
multiconst!(u32, [
    ZX_VMO_OP_COMMIT = 1;
    ZX_VMO_OP_DECOMMIT = 2;
    ZX_VMO_OP_LOCK = 3;
    ZX_VMO_OP_UNLOCK = 4;
    ZX_VMO_OP_CACHE_SYNC = 6;
    ZX_VMO_OP_CACHE_INVALIDATE = 7;
    ZX_VMO_OP_CACHE_CLEAN = 8;
    ZX_VMO_OP_CACHE_CLEAN_INVALIDATE = 9;
    ZX_VMO_OP_ZERO = 10;
    ZX_VMO_OP_TRY_LOCK = 11;
    ZX_VMO_OP_DONT_NEED = 12;
]);

// TODO: add an alias for this type in the C headers.
multiconst!(zx_vm_option_t, [
    ZX_VM_PERM_READ             = 1 << 0;
    ZX_VM_PERM_WRITE            = 1 << 1;
    ZX_VM_PERM_EXECUTE          = 1 << 2;
    ZX_VM_COMPACT               = 1 << 3;
    ZX_VM_SPECIFIC              = 1 << 4;
    ZX_VM_SPECIFIC_OVERWRITE    = 1 << 5;
    ZX_VM_CAN_MAP_SPECIFIC      = 1 << 6;
    ZX_VM_CAN_MAP_READ          = 1 << 7;
    ZX_VM_CAN_MAP_WRITE         = 1 << 8;
    ZX_VM_CAN_MAP_EXECUTE       = 1 << 9;
    ZX_VM_MAP_RANGE             = 1 << 10;
    ZX_VM_REQUIRE_NON_RESIZABLE = 1 << 11;
    ZX_VM_ALLOW_FAULTS          = 1 << 12;
    ZX_VM_OFFSET_IS_UPPER_LIMIT = 1 << 13;
    ZX_VM_PERM_READ_IF_XOM_UNSUPPORTED = 1 << 14;
]);

multiconst!(u32, [
    ZX_PROCESS_SHARED = 1 << 0;
]);

// matches ///zircon/system/public/zircon/errors.h
multiconst!(zx_status_t, [
    ZX_OK                         = 0;
    ZX_ERR_INTERNAL               = -1;
    ZX_ERR_NOT_SUPPORTED          = -2;
    ZX_ERR_NO_RESOURCES           = -3;
    ZX_ERR_NO_MEMORY              = -4;
    ZX_ERR_INTERRUPTED_RETRY      = -6;
    ZX_ERR_INVALID_ARGS           = -10;
    ZX_ERR_BAD_HANDLE             = -11;
    ZX_ERR_WRONG_TYPE             = -12;
    ZX_ERR_BAD_SYSCALL            = -13;
    ZX_ERR_OUT_OF_RANGE           = -14;
    ZX_ERR_BUFFER_TOO_SMALL       = -15;
    ZX_ERR_BAD_STATE              = -20;
    ZX_ERR_TIMED_OUT              = -21;
    ZX_ERR_SHOULD_WAIT            = -22;
    ZX_ERR_CANCELED               = -23;
    ZX_ERR_PEER_CLOSED            = -24;
    ZX_ERR_NOT_FOUND              = -25;
    ZX_ERR_ALREADY_EXISTS         = -26;
    ZX_ERR_ALREADY_BOUND          = -27;
    ZX_ERR_UNAVAILABLE            = -28;
    ZX_ERR_ACCESS_DENIED          = -30;
    ZX_ERR_IO                     = -40;
    ZX_ERR_IO_REFUSED             = -41;
    ZX_ERR_IO_DATA_INTEGRITY      = -42;
    ZX_ERR_IO_DATA_LOSS           = -43;
    ZX_ERR_IO_NOT_PRESENT         = -44;
    ZX_ERR_IO_OVERRUN             = -45;
    ZX_ERR_IO_MISSED_DEADLINE     = -46;
    ZX_ERR_IO_INVALID             = -47;
    ZX_ERR_BAD_PATH               = -50;
    ZX_ERR_NOT_DIR                = -51;
    ZX_ERR_NOT_FILE               = -52;
    ZX_ERR_FILE_BIG               = -53;
    ZX_ERR_NO_SPACE               = -54;
    ZX_ERR_NOT_EMPTY              = -55;
    ZX_ERR_STOP                   = -60;
    ZX_ERR_NEXT                   = -61;
    ZX_ERR_ASYNC                  = -62;
    ZX_ERR_PROTOCOL_NOT_SUPPORTED = -70;
    ZX_ERR_ADDRESS_UNREACHABLE    = -71;
    ZX_ERR_ADDRESS_IN_USE         = -72;
    ZX_ERR_NOT_CONNECTED          = -73;
    ZX_ERR_CONNECTION_REFUSED     = -74;
    ZX_ERR_CONNECTION_RESET       = -75;
    ZX_ERR_CONNECTION_ABORTED     = -76;
]);

multiconst!(zx_signals_t, [
    ZX_SIGNAL_NONE              = 0;
    ZX_OBJECT_SIGNAL_ALL        = 0x00ffffff;
    ZX_USER_SIGNAL_ALL          = 0xff000000;
    ZX_OBJECT_SIGNAL_0          = 1 << 0;
    ZX_OBJECT_SIGNAL_1          = 1 << 1;
    ZX_OBJECT_SIGNAL_2          = 1 << 2;
    ZX_OBJECT_SIGNAL_3          = 1 << 3;
    ZX_OBJECT_SIGNAL_4          = 1 << 4;
    ZX_OBJECT_SIGNAL_5          = 1 << 5;
    ZX_OBJECT_SIGNAL_6          = 1 << 6;
    ZX_OBJECT_SIGNAL_7          = 1 << 7;
    ZX_OBJECT_SIGNAL_8          = 1 << 8;
    ZX_OBJECT_SIGNAL_9          = 1 << 9;
    ZX_OBJECT_SIGNAL_10         = 1 << 10;
    ZX_OBJECT_SIGNAL_11         = 1 << 11;
    ZX_OBJECT_SIGNAL_12         = 1 << 12;
    ZX_OBJECT_SIGNAL_13         = 1 << 13;
    ZX_OBJECT_SIGNAL_14         = 1 << 14;
    ZX_OBJECT_SIGNAL_15         = 1 << 15;
    ZX_OBJECT_SIGNAL_16         = 1 << 16;
    ZX_OBJECT_SIGNAL_17         = 1 << 17;
    ZX_OBJECT_SIGNAL_18         = 1 << 18;
    ZX_OBJECT_SIGNAL_19         = 1 << 19;
    ZX_OBJECT_SIGNAL_20         = 1 << 20;
    ZX_OBJECT_SIGNAL_21         = 1 << 21;
    ZX_OBJECT_SIGNAL_22         = 1 << 22;
    ZX_OBJECT_HANDLE_CLOSED     = 1 << 23;
    ZX_USER_SIGNAL_0            = 1 << 24;
    ZX_USER_SIGNAL_1            = 1 << 25;
    ZX_USER_SIGNAL_2            = 1 << 26;
    ZX_USER_SIGNAL_3            = 1 << 27;
    ZX_USER_SIGNAL_4            = 1 << 28;
    ZX_USER_SIGNAL_5            = 1 << 29;
    ZX_USER_SIGNAL_6            = 1 << 30;
    ZX_USER_SIGNAL_7            = 1 << 31;

    ZX_OBJECT_READABLE          = ZX_OBJECT_SIGNAL_0;
    ZX_OBJECT_WRITABLE          = ZX_OBJECT_SIGNAL_1;
    ZX_OBJECT_PEER_CLOSED       = ZX_OBJECT_SIGNAL_2;

    // Cancelation (handle was closed while waiting with it)
    ZX_SIGNAL_HANDLE_CLOSED     = ZX_OBJECT_HANDLE_CLOSED;

    // Event
    ZX_EVENT_SIGNALED           = ZX_OBJECT_SIGNAL_3;

    // EventPair
    ZX_EVENTPAIR_SIGNALED       = ZX_OBJECT_SIGNAL_3;
    ZX_EVENTPAIR_CLOSED         = ZX_OBJECT_SIGNAL_2;

    // Task signals (process, thread, job)
    ZX_TASK_TERMINATED          = ZX_OBJECT_SIGNAL_3;

    // Channel
    ZX_CHANNEL_READABLE         = ZX_OBJECT_SIGNAL_0;
    ZX_CHANNEL_WRITABLE         = ZX_OBJECT_SIGNAL_1;
    ZX_CHANNEL_PEER_CLOSED      = ZX_OBJECT_SIGNAL_2;

    // Clock
    ZX_CLOCK_STARTED            = ZX_OBJECT_SIGNAL_4;

    // Socket
    ZX_SOCKET_READABLE            = ZX_OBJECT_READABLE;
    ZX_SOCKET_WRITABLE            = ZX_OBJECT_WRITABLE;
    ZX_SOCKET_PEER_CLOSED         = ZX_OBJECT_PEER_CLOSED;
    ZX_SOCKET_PEER_WRITE_DISABLED = ZX_OBJECT_SIGNAL_4;
    ZX_SOCKET_WRITE_DISABLED      = ZX_OBJECT_SIGNAL_5;
    ZX_SOCKET_READ_THRESHOLD      = ZX_OBJECT_SIGNAL_10;
    ZX_SOCKET_WRITE_THRESHOLD     = ZX_OBJECT_SIGNAL_11;

    // Resource
    ZX_RESOURCE_DESTROYED       = ZX_OBJECT_SIGNAL_3;
    ZX_RESOURCE_READABLE        = ZX_OBJECT_READABLE;
    ZX_RESOURCE_WRITABLE        = ZX_OBJECT_WRITABLE;
    ZX_RESOURCE_CHILD_ADDED     = ZX_OBJECT_SIGNAL_4;

    // Fifo
    ZX_FIFO_READABLE            = ZX_OBJECT_READABLE;
    ZX_FIFO_WRITABLE            = ZX_OBJECT_WRITABLE;
    ZX_FIFO_PEER_CLOSED         = ZX_OBJECT_PEER_CLOSED;

    // Job
    ZX_JOB_TERMINATED           = ZX_OBJECT_SIGNAL_3;
    ZX_JOB_NO_JOBS              = ZX_OBJECT_SIGNAL_4;
    ZX_JOB_NO_PROCESSES         = ZX_OBJECT_SIGNAL_5;

    // Process
    ZX_PROCESS_TERMINATED       = ZX_OBJECT_SIGNAL_3;

    // Thread
    ZX_THREAD_TERMINATED        = ZX_OBJECT_SIGNAL_3;
    ZX_THREAD_RUNNING           = ZX_OBJECT_SIGNAL_4;
    ZX_THREAD_SUSPENDED         = ZX_OBJECT_SIGNAL_5;

    // Log
    ZX_LOG_READABLE             = ZX_OBJECT_READABLE;
    ZX_LOG_WRITABLE             = ZX_OBJECT_WRITABLE;

    // Timer
    ZX_TIMER_SIGNALED           = ZX_OBJECT_SIGNAL_3;

    // Vmo
    ZX_VMO_ZERO_CHILDREN        = ZX_OBJECT_SIGNAL_3;
]);

multiconst!(zx_obj_type_t, [
    ZX_OBJ_TYPE_NONE                = 0;
    ZX_OBJ_TYPE_PROCESS             = 1;
    ZX_OBJ_TYPE_THREAD              = 2;
    ZX_OBJ_TYPE_VMO                 = 3;
    ZX_OBJ_TYPE_CHANNEL             = 4;
    ZX_OBJ_TYPE_EVENT               = 5;
    ZX_OBJ_TYPE_PORT                = 6;
    ZX_OBJ_TYPE_INTERRUPT           = 9;
    ZX_OBJ_TYPE_PCI_DEVICE          = 11;
    ZX_OBJ_TYPE_DEBUGLOG            = 12;
    ZX_OBJ_TYPE_SOCKET              = 14;
    ZX_OBJ_TYPE_RESOURCE            = 15;
    ZX_OBJ_TYPE_EVENTPAIR           = 16;
    ZX_OBJ_TYPE_JOB                 = 17;
    ZX_OBJ_TYPE_VMAR                = 18;
    ZX_OBJ_TYPE_FIFO                = 19;
    ZX_OBJ_TYPE_GUEST               = 20;
    ZX_OBJ_TYPE_VCPU                = 21;
    ZX_OBJ_TYPE_TIMER               = 22;
    ZX_OBJ_TYPE_IOMMU               = 23;
    ZX_OBJ_TYPE_BTI                 = 24;
    ZX_OBJ_TYPE_PROFILE             = 25;
    ZX_OBJ_TYPE_PMT                 = 26;
    ZX_OBJ_TYPE_SUSPEND_TOKEN       = 27;
    ZX_OBJ_TYPE_PAGER               = 28;
    ZX_OBJ_TYPE_EXCEPTION           = 29;
    ZX_OBJ_TYPE_CLOCK               = 30;
    ZX_OBJ_TYPE_STREAM              = 31;
]);

// TODO: add an alias for this type in the C headers.
multiconst!(u32, [
    // Argument is a char[ZX_MAX_NAME_LEN].
    ZX_PROP_NAME                      = 3;

    // Argument is a uintptr_t.
    #[cfg(target_arch = "x86_64")]
    ZX_PROP_REGISTER_GS               = 2;
    #[cfg(target_arch = "x86_64")]
    ZX_PROP_REGISTER_FS               = 4;

    // Argument is the value of ld.so's _dl_debug_addr, a uintptr_t. If the
    // property is set to the magic value of ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET
    // on process startup, ld.so will trigger a debug breakpoint immediately after
    // setting the property to the correct value.
    ZX_PROP_PROCESS_DEBUG_ADDR        = 5;

    // Argument is the base address of the vDSO mapping (or zero), a uintptr_t.
    ZX_PROP_PROCESS_VDSO_BASE_ADDRESS = 6;

    // Argument is a size_t.
    ZX_PROP_SOCKET_RX_THRESHOLD       = 12;
    ZX_PROP_SOCKET_TX_THRESHOLD       = 13;

    // Argument is a size_t, describing the number of packets a channel
    // endpoint can have pending in its tx direction.
    ZX_PROP_CHANNEL_TX_MSG_MAX        = 14;

    // Terminate this job if the system is low on memory.
    ZX_PROP_JOB_KILL_ON_OOM           = 15;

    // Exception close behavior.
    ZX_PROP_EXCEPTION_STATE           = 16;

    // The size of the content in a VMO, in bytes.
    ZX_PROP_VMO_CONTENT_SIZE          = 17;

    // How an exception should be handled.
    ZX_PROP_EXCEPTION_STRATEGY        = 18;

    // Whether the stream is in append mode or not.
    ZX_PROP_STREAM_MODE_APPEND        = 19;
]);

// Value for ZX_THREAD_STATE_SINGLE_STEP. The value can be 0 (not single-stepping), or 1
// (single-stepping). Other values will give ZX_ERR_INVALID_ARGS.
pub type zx_thread_state_single_step_t = u32;

// Possible values for "kind" in zx_thread_read_state and zx_thread_write_state.
multiconst!(zx_thread_state_topic_t, [
    ZX_THREAD_STATE_GENERAL_REGS       = 0;
    ZX_THREAD_STATE_FP_REGS            = 1;
    ZX_THREAD_STATE_VECTOR_REGS        = 2;
    // No 3 at the moment.
    ZX_THREAD_STATE_DEBUG_REGS         = 4;
    ZX_THREAD_STATE_SINGLE_STEP        = 5;
]);

// Possible values for "kind" in zx_vcpu_read_state and zx_vcpu_write_state.
multiconst!(zx_vcpu_state_topic_t, [
    ZX_VCPU_STATE   = 0;
    ZX_VCPU_IO      = 1;
]);

// From //zircon/system/public/zircon/syscalls/resource.h
multiconst!(zx_rsrc_kind_t, [
    ZX_RSRC_KIND_MMIO       = 0;
    ZX_RSRC_KIND_IRQ        = 1;
    ZX_RSRC_KIND_IOPORT     = 2;
    ZX_RSRC_KIND_ROOT       = 3;
    ZX_RSRC_KIND_SMC        = 4;
    ZX_RSRC_KIND_SYSTEM     = 5;
]);

// From //zircon/system/public/zircon/syscalls/resource.h
multiconst!(zx_rsrc_system_base_t, [
    ZX_RSRC_SYSTEM_HYPERVISOR_BASE = 0;
    ZX_RSRC_SYSTEM_VMEX_BASE       = 1;
    ZX_RSRC_SYSTEM_DEBUG_BASE      = 2;
    ZX_RSRC_SYSTEM_INFO_BASE       = 3;
    ZX_RSRC_SYSTEM_CPU_BASE        = 4;
    ZX_RSRC_SYSTEM_POWER_BASE      = 5;
    ZX_RSRC_SYSTEM_MEXEC_BASE      = 6;
]);

pub const ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET: usize = 1;

// clock ids
multiconst!(zx_clock_t, [
    ZX_CLOCK_MONOTONIC    = 0;
]);

// from //zircon/system/public/zircon/syscalls/clock.h
multiconst!(u64, [
    ZX_CLOCK_OPT_MONOTONIC = 1 << 0;
    ZX_CLOCK_OPT_CONTINUOUS = 1 << 1;
    ZX_CLOCK_OPT_AUTO_START = 1 << 2;

    // v1 clock update flags
    ZX_CLOCK_UPDATE_OPTION_VALUE_VALID = 1 << 0;
    ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID = 1 << 1;
    ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID = 1 << 2;

    // Additional v2 clock update flags
    ZX_CLOCK_UPDATE_OPTION_REFERENCE_VALUE_VALID = 1 << 3;
    ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID = ZX_CLOCK_UPDATE_OPTION_VALUE_VALID;

    ZX_CLOCK_ARGS_VERSION_1 = 1 << 58;
    ZX_CLOCK_ARGS_VERSION_2 = 2 << 58;
]);

// from //zircon/system/public/zircon/syscalls/exception.h
multiconst!(u32, [
    ZX_EXCEPTION_CHANNEL_DEBUGGER = 1 << 0;
]);

/// A byte used only to control memory alignment. All padding bytes are considered equal
/// regardless of their content.
///
/// Note that the kernel C/C++ struct definitions use explicit padding fields to ensure no implicit
/// padding is added. This is important for security since implicit padding bytes are not always
/// safely initialized. These explicit padding fields are mirrored in the Rust struct definitions
/// to minimize the opportunities for mistakes and inconsistencies.
#[repr(C)]
#[derive(Copy, Clone, Eq, Default)]
pub struct PadByte(u8);

impl PartialEq for PadByte {
    fn eq(&self, _other: &Self) -> bool {
        true
    }
}

impl Hash for PadByte {
    fn hash<H: Hasher>(&self, state: &mut H) {
        state.write_u8(0);
    }
}

impl Debug for PadByte {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("-")
    }
}

#[repr(C)]
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct zx_clock_create_args_v1_t {
    pub backstop_time: zx_time_t,
}

#[repr(C)]
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct zx_clock_rate_t {
    pub synthetic_ticks: u32,
    pub reference_ticks: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct zx_clock_transformation_t {
    pub reference_offset: i64,
    pub synthetic_offset: i64,
    pub rate: zx_clock_rate_t,
}

#[repr(C)]
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct zx_clock_details_v1_t {
    pub options: u64,
    pub backstop_time: zx_time_t,
    pub ticks_to_synthetic: zx_clock_transformation_t,
    pub mono_to_synthetic: zx_clock_transformation_t,
    pub error_bound: u64,
    pub query_ticks: zx_ticks_t,
    pub last_value_update_ticks: zx_ticks_t,
    pub last_rate_adjust_update_ticks: zx_ticks_t,
    pub last_error_bounds_update_ticks: zx_ticks_t,
    pub generation_counter: u32,
    pub padding1: [PadByte; 4],
}

#[repr(C)]
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct zx_clock_update_args_v1_t {
    pub rate_adjust: i32,
    pub padding1: [PadByte; 4],
    pub value: i64,
    pub error_bound: u64,
}

#[repr(C)]
#[derive(Debug, Default, Clone, Eq, PartialEq)]
pub struct zx_clock_update_args_v2_t {
    pub rate_adjust: i32,
    pub padding1: [PadByte; 4],
    pub synthetic_value: i64,
    pub reference_value: i64,
    pub error_bound: u64,
}

multiconst!(zx_stream_seek_origin_t, [
    ZX_STREAM_SEEK_ORIGIN_START        = 0;
    ZX_STREAM_SEEK_ORIGIN_CURRENT      = 1;
    ZX_STREAM_SEEK_ORIGIN_END          = 2;
]);

// Stream constants
pub const ZX_STREAM_MODE_READ: u32 = 1 << 0;
pub const ZX_STREAM_MODE_WRITE: u32 = 1 << 1;
pub const ZX_STREAM_MODE_APPEND: u32 = 1 << 2;

pub const ZX_STREAM_APPEND: u32 = 1 << 0;

// Buffer size limits on the cprng syscalls
pub const ZX_CPRNG_DRAW_MAX_LEN: usize = 256;
pub const ZX_CPRNG_ADD_ENTROPY_MAX_LEN: usize = 256;

// Socket flags and limits.
pub const ZX_SOCKET_DISPOSITION_WRITE_DISABLED: u32 = 1 << 0;
pub const ZX_SOCKET_DISPOSITION_WRITE_ENABLED: u32 = 1 << 1;

// VM Object clone flags
pub const ZX_VMO_CHILD_SNAPSHOT: u32 = 1 << 0;
pub const ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE: u32 = 1 << 4;
pub const ZX_VMO_CHILD_RESIZABLE: u32 = 1 << 2;
pub const ZX_VMO_CHILD_SLICE: u32 = 1 << 3;
pub const ZX_VMO_CHILD_NO_WRITE: u32 = 1 << 5;

// channel write size constants
pub const ZX_CHANNEL_MAX_MSG_HANDLES: u32 = 64;
pub const ZX_CHANNEL_MAX_MSG_BYTES: u32 = 65536;

// fifo write size constants
pub const ZX_FIFO_MAX_SIZE_BYTES: u32 = 4096;

// Min/max page size constants
#[cfg(target_arch = "x86_64")]
pub const ZX_MIN_PAGE_SHIFT: u32 = 12;
#[cfg(target_arch = "x86_64")]
pub const ZX_MAX_PAGE_SHIFT: u32 = 21;

#[cfg(target_arch = "aarch64")]
pub const ZX_MIN_PAGE_SHIFT: u32 = 12;
#[cfg(target_arch = "aarch64")]
pub const ZX_MAX_PAGE_SHIFT: u32 = 16;

// Task response codes if a process is externally killed
pub const ZX_TASK_RETCODE_SYSCALL_KILL: i64 = -1024;
pub const ZX_TASK_RETCODE_OOM_KILL: i64 = -1025;
pub const ZX_TASK_RETCODE_POLICY_KILL: i64 = -1026;
pub const ZX_TASK_RETCODE_VDSO_KILL: i64 = -1027;
pub const ZX_TASK_RETCODE_EXCEPTION_KILL: i64 = -1028;

// Resource flags.
pub const ZX_RSRC_FLAG_EXCLUSIVE: zx_rsrc_flags_t = 0x00010000;

// Topics for CPU performance info syscalls
pub const ZX_CPU_PERF_SCALE: u32 = 1;
pub const ZX_CPU_DEFAULT_PERF_SCALE: u32 = 2;

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_cache_policy_t {
    ZX_CACHE_POLICY_CACHED = 0,
    ZX_CACHE_POLICY_UNCACHED = 1,
    ZX_CACHE_POLICY_UNCACHED_DEVICE = 2,
    ZX_CACHE_POLICY_WRITE_COMBINING = 3,
}

// Flag bits for zx_cache_flush.
multiconst!(u32, [
    ZX_CACHE_FLUSH_INSN         = 1 << 0;
    ZX_CACHE_FLUSH_DATA         = 1 << 1;
    ZX_CACHE_FLUSH_INVALIDATE   = 1 << 2;
]);

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_wait_item_t {
    pub handle: zx_handle_t,
    pub waitfor: zx_signals_t,
    pub pending: zx_signals_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_waitset_result_t {
    pub cookie: u64,
    pub status: zx_status_t,
    pub observed: zx_signals_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_handle_info_t {
    pub handle: zx_handle_t,
    pub ty: zx_obj_type_t,
    pub rights: zx_rights_t,
    pub unused: u32,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_channel_call_args_t {
    pub wr_bytes: *const u8,
    pub wr_handles: *const zx_handle_t,
    pub rd_bytes: *mut u8,
    pub rd_handles: *mut zx_handle_t,
    pub wr_num_bytes: u32,
    pub wr_num_handles: u32,
    pub rd_num_bytes: u32,
    pub rd_num_handles: u32,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_channel_call_etc_args_t {
    pub wr_bytes: *const u8,
    pub wr_handles: *mut zx_handle_disposition_t,
    pub rd_bytes: *mut u8,
    pub rd_handles: *mut zx_handle_info_t,
    pub wr_num_bytes: u32,
    pub wr_num_handles: u32,
    pub rd_num_bytes: u32,
    pub rd_num_handles: u32,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_handle_disposition_t {
    pub operation: zx_handle_op_t,
    pub handle: zx_handle_t,
    pub type_: zx_obj_type_t,
    pub rights: zx_rights_t,
    pub result: zx_status_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct zx_iovec_t {
    pub buffer: *const u8,
    pub capacity: usize,
}

pub type zx_pci_irq_swizzle_lut_t = [[[u32; 4]; 8]; 32];

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_pci_init_arg_t {
    pub dev_pin_to_global_irq: zx_pci_irq_swizzle_lut_t,
    pub num_irqs: u32,
    pub irqs: [zx_irq_t; 32],
    pub ecam_window_count: u32,
    // Note: the ecam_windows field is actually a variable size array.
    // We use a fixed size array to match the C repr.
    pub ecam_windows: [zx_ecam_window_t; 1],
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_irq_t {
    pub global_irq: u32,
    pub level_triggered: bool,
    pub active_high: bool,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_ecam_window_t {
    pub base: u64,
    pub size: usize,
    pub bus_start: u8,
    pub bus_end: u8,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_pcie_device_info_t {
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
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_pci_resource_t {
    pub type_: u32,
    pub size: usize,
    // TODO: Actually a union
    pub pio_addr: usize,
}

// TODO: Actually a union
pub type zx_rrec_t = [u8; 64];

// Ports V2
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_packet_type_t {
    ZX_PKT_TYPE_USER = 0,
    ZX_PKT_TYPE_SIGNAL_ONE = 1,
    ZX_PKT_TYPE_GUEST_BELL = 3,
    ZX_PKT_TYPE_GUEST_MEM = 4,
    ZX_PKT_TYPE_GUEST_IO = 5,
    ZX_PKT_TYPE_GUEST_VCPU = 6,
    ZX_PKT_TYPE_INTERRUPT = 7,
    ZX_PKT_TYPE_PAGE_REQUEST = 9,
    #[doc(hidden)]
    __Nonexhaustive,
}

impl Default for zx_packet_type_t {
    fn default() -> Self {
        zx_packet_type_t::ZX_PKT_TYPE_USER
    }
}

#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_packet_guest_vcpu_type_t {
    ZX_PKT_GUEST_VCPU_INTERRUPT = 0,
    ZX_PKT_GUEST_VCPU_STARTUP = 1,
    ZX_PKT_GUEST_VCPU_EXIT = 2,
    #[doc(hidden)]
    __Nonexhaustive,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct zx_packet_signal_t {
    pub trigger: zx_signals_t,
    pub observed: zx_signals_t,
    pub count: u64,
}

pub const ZX_WAIT_ASYNC_TIMESTAMP: u32 = 1;
pub const ZX_WAIT_ASYNC_EDGE: u32 = 2;

// Actually a union of different integer types, but this should be good enough.
pub type zx_packet_user_t = [u8; 32];

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_port_packet_t {
    pub key: u64,
    pub packet_type: zx_packet_type_t,
    pub status: i32,
    pub union: [u8; 32],
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_bell_t {
    pub addr: zx_gpaddr_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_io_t {
    pub port: u16,
    pub access_size: u8,
    pub input: bool,
    pub data: [u8; 4],
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_vcpu_interrupt_t {
    pub mask: u64,
    pub vector: u8,
    padding1: [PadByte; 7],
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_vcpu_startup_t {
    pub id: u64,
    pub entry: zx_gpaddr_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_vcpu_exit_t {
    pub retcode: i64,
    reserved: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_packet_guest_vcpu_union_t {
    pub interrupt: zx_packet_guest_vcpu_interrupt_t,
    pub startup: zx_packet_guest_vcpu_startup_t,
    pub exit: zx_packet_guest_vcpu_exit_t,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zx_packet_guest_vcpu_t {
    pub r#type: zx_packet_guest_vcpu_type_t,
    padding1: [PadByte; 4],
    pub union: zx_packet_guest_vcpu_union_t,
    reserved: u64,
}

impl Debug for zx_packet_guest_vcpu_t {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.r#type {
            zx_packet_guest_vcpu_type_t::ZX_PKT_GUEST_VCPU_INTERRUPT => {
                write!(f, "type: {:?} union: {:?}", self.r#type, unsafe { self.union.interrupt })
            }
            zx_packet_guest_vcpu_type_t::ZX_PKT_GUEST_VCPU_STARTUP => {
                write!(f, "type: {:?} union: {:?}", self.r#type, unsafe { self.union.startup })
            }
            zx_packet_guest_vcpu_type_t::ZX_PKT_GUEST_VCPU_EXIT => {
                write!(f, "type: {:?} union: {:?}", self.r#type, unsafe { self.union.exit })
            }
            _ => panic!("unexpected VCPU packet type"),
        }
    }
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_page_request_t {
    pub command: zx_page_request_command_t,
    pub flags: u16,
    _reserved0: u32,
    pub offset: u64,
    pub length: u64,
    _reserved1: u64,
}

#[repr(u16)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_page_request_command_t {
    ZX_PAGER_VMO_READ = 0x0000,
    ZX_PAGER_VMO_COMPLETE = 0x0001,
    ZX_PAGER_VMO_DIRTY = 0x0002,
    #[doc(hidden)]
    __Nonexhaustive,
}

multiconst!(u32, [
    ZX_PAGER_OP_FAIL = 1;
    ZX_PAGER_OP_DIRTY = 2;
    ZX_PAGER_OP_WRITEBACK_BEGIN = 3;
    ZX_PAGER_OP_WRITEBACK_END = 4;
]);

pub type zx_excp_type_t = u32;

multiconst!(zx_obj_type_t, [
    ZX_EXCP_GENERAL               = 0x008;
    ZX_EXCP_FATAL_PAGE_FAULT      = 0x108;
    ZX_EXCP_UNDEFINED_INSTRUCTION = 0x208;
    ZX_EXCP_SW_BREAKPOINT         = 0x308;
    ZX_EXCP_HW_BREAKPOINT         = 0x408;
    ZX_EXCP_UNALIGNED_ACCESS      = 0x508;

    ZX_EXCP_SYNTH                 = 0x8000;

    ZX_EXCP_THREAD_STARTING       = 0x008 | ZX_EXCP_SYNTH;
    ZX_EXCP_THREAD_EXITING        = 0x108 | ZX_EXCP_SYNTH;
    ZX_EXCP_POLICY_ERROR          = 0x208 | ZX_EXCP_SYNTH;
]);

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_exception_info_t {
    pub pid: zx_koid_t,
    pub tid: zx_koid_t,
    pub type_: zx_excp_type_t,
    pub padding1: [PadByte; 4],
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_x86_64_exc_data_t {
    pub vector: u64,
    pub err_code: u64,
    pub cr2: u64,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_arm64_exc_data_t {
    pub esr: u32,
    pub padding1: [PadByte; 4],
    pub far: u64,
    pub padding2: [PadByte; 8],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_exception_header_arch_t {
    pub x86_64: zx_x86_64_exc_data_t,
    pub arm_64: zx_arm64_exc_data_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_exception_header_t {
    pub size: u32,
    pub type_: zx_excp_type_t,
}

pub type zx_excp_policy_code_t = u32;

multiconst!(zx_excp_policy_code_t, [
    ZX_EXCP_POLICY_CODE_BAD_HANDLE              = 0;
    ZX_EXCP_POLICY_CODE_WRONG_OBJECT            = 1;
    ZX_EXCP_POLICY_CODE_VMAR_WX                 = 2;
    ZX_EXCP_POLICY_CODE_NEW_ANY                 = 3;
    ZX_EXCP_POLICY_CODE_NEW_VMO                 = 4;
    ZX_EXCP_POLICY_CODE_NEW_CHANNEL             = 5;
    ZX_EXCP_POLICY_CODE_NEW_EVENT               = 6;
    ZX_EXCP_POLICY_CODE_NEW_EVENTPAIR           = 7;
    ZX_EXCP_POLICY_CODE_NEW_PORT                = 8;
    ZX_EXCP_POLICY_CODE_NEW_SOCKET              = 9;
    ZX_EXCP_POLICY_CODE_NEW_FIFO                = 10;
    ZX_EXCP_POLICY_CODE_NEW_TIMER               = 11;
    ZX_EXCP_POLICY_CODE_NEW_PROCESS             = 12;
    ZX_EXCP_POLICY_CODE_NEW_PROFILE             = 13;
    ZX_EXCP_POLICY_CODE_NEW_PAGER               = 14;
    ZX_EXCP_POLICY_CODE_AMBIENT_MARK_VMO_EXEC   = 15;
    ZX_EXCP_POLICY_CODE_CHANNEL_FULL_WRITE      = 16;
    ZX_EXCP_POLICY_CODE_PORT_TOO_MANY_PACKETS   = 17;
    ZX_EXCP_POLICY_CODE_BAD_SYSCALL             = 18;
    ZX_EXCP_POLICY_CODE_PORT_TOO_MANY_OBSERVERS = 19;
]);

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zx_exception_context_t {
    pub arch: zx_exception_header_arch_t,
    pub synth_code: zx_excp_policy_code_t,
    pub synth_data: u32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zx_exception_report_t {
    pub header: zx_exception_header_t,
    pub context: zx_exception_context_t,
}

pub type zx_exception_state_t = u32;

multiconst!(zx_exception_state_t, [
    ZX_EXCEPTION_STATE_TRY_NEXT    = 0;
    ZX_EXCEPTION_STATE_HANDLED     = 1;
    ZX_EXCEPTION_STATE_THREAD_EXIT = 2;
]);

pub type zx_exception_strategy_t = u32;

multiconst!(zx_exception_state_t, [
    ZX_EXCEPTION_STRATEGY_FIRST_CHANCE   = 0;
    ZX_EXCEPTION_STRATEGY_SECOND_CHANCE  = 1;
]);

#[cfg(target_arch = "x86_64")]
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_thread_state_general_regs_t {
    pub rax: u64,
    pub rbx: u64,
    pub rcx: u64,
    pub rdx: u64,
    pub rsi: u64,
    pub rdi: u64,
    pub rbp: u64,
    pub rsp: u64,
    pub r8: u64,
    pub r9: u64,
    pub r10: u64,
    pub r11: u64,
    pub r12: u64,
    pub r13: u64,
    pub r14: u64,
    pub r15: u64,
    pub rip: u64,
    pub rflags: u64,
    pub fs_base: u64,
    pub gs_base: u64,
}

#[cfg(target_arch = "x86_64")]
impl From<&zx_restricted_state_t> for zx_thread_state_general_regs_t {
    fn from(state: &zx_restricted_state_t) -> Self {
        Self {
            rdi: state.rdi,
            rsi: state.rsi,
            rbp: state.rbp,
            rbx: state.rbx,
            rdx: state.rdx,
            rcx: state.rcx,
            rax: state.rax,
            rsp: state.rsp,
            r8: state.r8,
            r9: state.r9,
            r10: state.r10,
            r11: state.r11,
            r12: state.r12,
            r13: state.r13,
            r14: state.r14,
            r15: state.r15,
            rip: state.ip,
            rflags: state.flags,
            fs_base: state.fs_base,
            gs_base: state.gs_base,
        }
    }
}

#[cfg(target_arch = "aarch64")]
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_thread_state_general_regs_t {
    pub r: [u64; 30],
    pub lr: u64,
    pub sp: u64,
    pub pc: u64,
    pub cpsr: u64,
    pub tpidr: u64,
}

#[cfg(target_arch = "x86_64")]
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_restricted_state_t {
    pub rdi: u64,
    pub rsi: u64,
    pub rbp: u64,
    pub rbx: u64,
    pub rdx: u64,
    pub rcx: u64,
    pub rax: u64,
    pub rsp: u64,
    pub r8: u64,
    pub r9: u64,
    pub r10: u64,
    pub r11: u64,
    pub r12: u64,
    pub r13: u64,
    pub r14: u64,
    pub r15: u64,
    pub ip: u64,
    pub flags: u64,
    pub fs_base: u64,
    pub gs_base: u64,
}

#[cfg(target_arch = "x86_64")]
impl From<&zx_thread_state_general_regs_t> for zx_restricted_state_t {
    fn from(registers: &zx_thread_state_general_regs_t) -> Self {
        Self {
            rdi: registers.rdi,
            rsi: registers.rsi,
            rbp: registers.rbp,
            rbx: registers.rbx,
            rdx: registers.rdx,
            rcx: registers.rcx,
            rax: registers.rax,
            rsp: registers.rsp,
            r8: registers.r8,
            r9: registers.r9,
            r10: registers.r10,
            r11: registers.r11,
            r12: registers.r12,
            r13: registers.r13,
            r14: registers.r14,
            r15: registers.r15,
            ip: registers.rip,
            flags: registers.rflags,
            fs_base: registers.fs_base,
            gs_base: registers.gs_base,
        }
    }
}

#[cfg(target_arch = "x86_64")]
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_vcpu_state_t {
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
    // Contains only the user-controllable lower 32-bits.
    pub rflags: u64,
}

#[cfg(target_arch = "aarch64")]
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_vcpu_state_t {
    pub x: [u64; 31],
    pub sp: u64,
    // Contains only the user-controllable upper 4-bits (NZCV).
    pub cpsr: u32,
    pub _padding1: [PadByte; 4],
}

#[cfg(target_arch = "aarch64")]
#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_mem_t {
    pub addr: zx_gpaddr_t,
    pub access_size: u8,
    pub sign_extend: bool,
    pub xt: u8,
    pub read: bool,
    pub data: u64,
}

pub const X86_MAX_INST_LEN: usize = 15;

#[cfg(target_arch = "x86_64")]
#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_mem_t {
    pub addr: zx_gpaddr_t,
    pub cr3: zx_gpaddr_t,
    pub rip: zx_vaddr_t,
    pub instruction_size: u8,
    pub default_operand_size: u8,
}

// Helper for constructing topics that have been versioned.
const fn info_topic(topic: u32, version: u32) -> u32 {
    (version << 28) | topic
}

multiconst!(zx_object_info_topic_t, [
    ZX_INFO_NONE                       = 0;
    ZX_INFO_HANDLE_VALID               = 1;
    ZX_INFO_HANDLE_BASIC               = 2;  // zx_info_handle_basic_t[1]
    ZX_INFO_PROCESS                    = info_topic(3, 1);  // zx_info_process_t[1]
    ZX_INFO_PROCESS_THREADS            = 4;  // zx_koid_t[n]
    ZX_INFO_VMAR                       = 7;  // zx_info_vmar_t[1]
    ZX_INFO_JOB_CHILDREN               = 8;  // zx_koid_t[n]
    ZX_INFO_JOB_PROCESSES              = 9;  // zx_koid_t[n]
    ZX_INFO_THREAD                     = 10; // zx_info_thread_t[1]
    ZX_INFO_THREAD_EXCEPTION_REPORT    = info_topic(11, 1); // zx_exception_report_t[1]
    ZX_INFO_TASK_STATS                 = 12; // zx_info_task_stats_t[1]
    ZX_INFO_PROCESS_MAPS               = 13; // zx_info_maps_t[n]
    ZX_INFO_PROCESS_VMOS               = info_topic(14, 1); // zx_info_vmo_t[n]
    ZX_INFO_THREAD_STATS               = 15; // zx_info_thread_stats_t[1]
    ZX_INFO_CPU_STATS                  = 16; // zx_info_cpu_stats_t[n]
    ZX_INFO_KMEM_STATS                 = 17; // zx_info_kmem_stats_t[1]
    ZX_INFO_RESOURCE                   = 18; // zx_info_resource_t[1]
    ZX_INFO_HANDLE_COUNT               = 19; // zx_info_handle_count_t[1]
    ZX_INFO_BTI                        = 20; // zx_info_bti_t[1]
    ZX_INFO_PROCESS_HANDLE_STATS       = 21; // zx_info_process_handle_stats_t[1]
    ZX_INFO_SOCKET                     = 22; // zx_info_socket_t[1]
    ZX_INFO_VMO                        = info_topic(23, 1); // zx_info_vmo_t[1]
    ZX_INFO_JOB                        = 24; // zx_info_job_t[1]
    ZX_INFO_TIMER                      = 25; // zx_info_timer_t[1]
    ZX_INFO_STREAM                     = 26; // zx_info_stream_t[1]
    ZX_INFO_HANDLE_TABLE               = 27; // zx_info_handle_extended_t[n]
    ZX_INFO_MSI                        = 28; // zx_info_msi_t[1]
    ZX_INFO_GUEST_STATS                = 29; // zx_info_guest_stats_t[1]
    ZX_INFO_TASK_RUNTIME               = info_topic(30, 1); // zx_info_task_runtime_t[1]
    ZX_INFO_KMEM_STATS_EXTENDED        = 31; // zx_info_kmem_stats_extended_t[1]
]);

// This macro takes struct-like syntax and creates another macro that can be used to create
// different instances of the struct with different names. This is used to keep struct definitions
// from drifting between this crate and the fuchsia-zircon crate where they are identical other
// than in name and location.
macro_rules! struct_decl_macro {
    ( $(#[$attrs:meta])* $vis:vis struct <$macro_name:ident> $($any:tt)* ) => {
        #[macro_export]
        macro_rules! $macro_name {
            ($name:ident) => {
                $(#[$attrs])* $vis struct $name $($any)*
            }
        }
    }
}

// Don't need struct_decl_macro for this, the wrapper is different.
#[repr(C)]
#[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_info_handle_basic_t {
    pub koid: zx_koid_t,
    pub rights: zx_rights_t,
    pub type_: zx_obj_type_t,
    pub related_koid: zx_koid_t,
    pub reserved: u32,
}

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_handle_count_t> {
        pub handle_count: u32,
    }
}

zx_info_handle_count_t!(zx_info_handle_count_t);

// Don't need struct_decl_macro for this, the wrapper is different.
#[repr(C)]
#[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_info_socket_t {
    pub options: u32,
    pub rx_buf_max: usize,
    pub rx_buf_size: usize,
    pub rx_buf_available: usize,
    pub tx_buf_max: usize,
    pub tx_buf_size: usize,
}

multiconst!(u32, [
    ZX_INFO_PROCESS_FLAG_STARTED = 1 << 0;
    ZX_INFO_PROCESS_FLAG_EXITED = 1 << 1;
    ZX_INFO_PROCESS_FLAG_DEBUGGER_ATTACHED = 1 << 2;
]);

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_process_t> {
        pub return_code: i64,
        pub start_time: zx_time_t,
        pub flags: u32,
    }
}

zx_info_process_t!(zx_info_process_t);

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_job_t> {
        pub return_code: i64,
        pub exited: bool,
        pub kill_on_oom: bool,
        pub debugger_attached: bool,
    }
}

zx_info_job_t!(zx_info_job_t);

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_policy_basic {
    pub condition: u32,
    pub policy: u32,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_policy_timer_slack {
    pub min_slack: zx_duration_t,
    pub default_mode: u32,
}

multiconst!(u32, [
    // policy options
    ZX_JOB_POL_RELATIVE = 0;
    ZX_JOB_POL_ABSOLUTE = 1;

    // policy topic
    ZX_JOB_POL_BASIC = 0;
    ZX_JOB_POL_TIMER_SLACK = 1;

    // policy conditions
    ZX_POL_BAD_HANDLE            = 0;
    ZX_POL_WRONG_OBJECT          = 1;
    ZX_POL_VMAR_WX               = 2;
    ZX_POL_NEW_ANY               = 3;
    ZX_POL_NEW_VMO               = 4;
    ZX_POL_NEW_CHANNEL           = 5;
    ZX_POL_NEW_EVENT             = 6;
    ZX_POL_NEW_EVENTPAIR         = 7;
    ZX_POL_NEW_PORT              = 8;
    ZX_POL_NEW_SOCKET            = 9;
    ZX_POL_NEW_FIFO              = 10;
    ZX_POL_NEW_TIMER             = 11;
    ZX_POL_NEW_PROCESS           = 12;
    ZX_POL_NEW_PROFILE           = 13;
    ZX_POL_NEW_PAGER             = 14;
    ZX_POL_AMBIENT_MARK_VMO_EXEC = 15;

    // policy actions
    ZX_POL_ACTION_ALLOW           = 0;
    ZX_POL_ACTION_DENY            = 1;
    ZX_POL_ACTION_ALLOW_EXCEPTION = 2;
    ZX_POL_ACTION_DENY_EXCEPTION  = 3;
    ZX_POL_ACTION_KILL            = 4;

    // timer slack default modes
    ZX_TIMER_SLACK_CENTER = 0;
    ZX_TIMER_SLACK_EARLY  = 1;
    ZX_TIMER_SLACK_LATE   = 2;
]);

multiconst!(u32, [
    // critical options
    ZX_JOB_CRITICAL_PROCESS_RETCODE_NONZERO = 1 << 0;
]);

// Don't use struct_decl_macro, wrapper is different.
#[repr(C)]
#[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_info_vmo_t {
    pub koid: zx_koid_t,
    pub name: [u8; ZX_MAX_NAME_LEN],
    pub size_bytes: u64,
    pub parent_koid: zx_koid_t,
    pub num_children: usize,
    pub num_mappings: usize,
    pub share_count: usize,
    pub flags: u32,
    _padding1: [PadByte; 4],
    pub committed_bytes: u64,
    pub handle_rights: zx_rights_t,
    pub cache_policy: u32,
    pub metadata_bytes: u64,
    pub committed_change_events: u64,
}

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_cpu_stats_t> {
        pub cpu_number: u32,
        pub flags: u32,
        pub idle_time: zx_duration_t,
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
}

zx_info_cpu_stats_t!(zx_info_cpu_stats_t);

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_kmem_stats_t> {
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
}

zx_info_kmem_stats_t!(zx_info_kmem_stats_t);

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_kmem_stats_extended_t> {
        pub total_bytes: u64,
        pub free_bytes: u64,
        pub wired_bytes: u64,
        pub total_heap_bytes: u64,
        pub free_heap_bytes: u64,
        pub vmo_bytes: u64,
        pub vmo_pager_total_bytes: u64,
        pub vmo_pager_newest_bytes: u64,
        pub vmo_pager_oldest_bytes: u64,
        pub vmo_discardable_locked_bytes: u64,
        pub vmo_discardable_unlocked_bytes: u64,
        pub mmu_overhead_bytes: u64,
        pub ipc_bytes: u64,
        pub other_bytes: u64,
    }
}

zx_info_kmem_stats_extended_t!(zx_info_kmem_stats_extended_t);

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_resource_t> {
        pub kind: u32,
        pub flags: u32,
        pub base: u64,
        pub size: usize,
        pub name: [u8; ZX_MAX_NAME_LEN],
    }
}

zx_info_resource_t!(zx_info_resource_t);

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_vmar_t> {
        pub base: usize,
        pub len: usize,
    }
}

zx_info_vmar_t!(zx_info_vmar_t);

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_task_stats_t> {
        pub mem_mapped_bytes: usize,
        pub mem_private_bytes: usize,
        pub mem_shared_bytes: usize,
        pub mem_scaled_shared_bytes: usize,
    }
}

zx_info_task_stats_t!(zx_info_task_stats_t);

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_task_runtime_t> {
        pub cpu_time: zx_duration_t,
        pub queue_time: zx_duration_t,
        pub page_fault_time: zx_duration_t,
        pub lock_contention_time: zx_duration_t,
    }
}

zx_info_task_runtime_t!(zx_info_task_runtime_t);

multiconst!(zx_info_maps_type_t, [
    ZX_INFO_MAPS_TYPE_NONE    = 0;
    ZX_INFO_MAPS_TYPE_ASPACE  = 1;
    ZX_INFO_MAPS_TYPE_VMAR    = 2;
    ZX_INFO_MAPS_TYPE_MAPPING = 3;
]);

struct_decl_macro! {
    #[repr(C)]
    #[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_info_maps_mapping_t> {
        pub mmu_flags: zx_vm_option_t,
        pub padding1: [PadByte; 4],
        pub vmo_koid: zx_koid_t,
        pub vmo_offset: u64,
        pub committed_pages: usize,
    }
}

zx_info_maps_mapping_t!(zx_info_maps_mapping_t);

#[repr(C)]
#[derive(Copy, Clone)]
pub union InfoMapsTypeUnion {
    pub mapping: zx_info_maps_mapping_t,
}

struct_decl_macro! {
    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct <zx_info_maps_t> {
        pub name: [u8; ZX_MAX_NAME_LEN],
        pub base: zx_vaddr_t,
        pub size: usize,
        pub depth: usize,
        pub r#type: zx_info_maps_type_t,
        pub u: InfoMapsTypeUnion,
    }
}

zx_info_maps_t!(zx_info_maps_t);

// from //zircon/system/public/zircon/syscalls/hypervisor.h
multiconst!(zx_guest_option_t, [
    ZX_GUEST_OPT_NORMAL = 0;
    ZX_GUEST_OPT_DIRECT = 1;
]);

multiconst!(zx_guest_trap_t, [
    ZX_GUEST_TRAP_BELL = 0;
    ZX_GUEST_TRAP_MEM  = 1;
    ZX_GUEST_TRAP_IO   = 2;
]);

pub const ZX_LOG_RECORD_MAX: usize = 256;
pub const ZX_LOG_RECORD_DATA_MAX: usize = 216;

struct_decl_macro! {
    #[repr(C)]
    #[derive(Debug, Copy, Clone, Eq, PartialEq)]
    pub struct <zx_log_record_t> {
        pub sequence: u64,
        pub padding1: [PadByte; 4],
        pub datalen: u16,
        pub severity: u8,
        pub flags: u8,
        pub timestamp: zx_time_t,
        pub pid: u64,
        pub tid: u64,
        pub data: [u8; ZX_LOG_RECORD_DATA_MAX],
    }
}
const_assert_eq!(std::mem::size_of::<zx_log_record_t>(), ZX_LOG_RECORD_MAX);

zx_log_record_t!(zx_log_record_t);

impl Default for zx_log_record_t {
    fn default() -> zx_log_record_t {
        zx_log_record_t {
            sequence: 0,
            padding1: [PadByte(0); 4],
            datalen: 0,
            severity: 0,
            flags: 0,
            timestamp: 0,
            pid: 0,
            tid: 0,
            data: [0; ZX_LOG_RECORD_DATA_MAX],
        }
    }
}

multiconst!(u32, [
    ZX_LOG_FLAG_READABLE = 0x40000000;
]);

// For C, the below types are currently forward declared for syscalls.h.
// We might want to investigate a better solution for Rust or removing those
// forward declarations.
//
// These are hand typed translations from C types into Rust structures using a C
// layout

// source: zircon/system/public/zircon/syscalls/system.h
#[repr(C)]
pub struct zx_system_powerctl_arg_t {
    // rust can't express anonymous unions at this time
    // https://github.com/rust-lang/rust/issues/49804
    powerctl_internal: zx_powerctl_union,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_powerctl_union {
    acpi_transition_s_state: acpi_transition_s_state,
    x86_power_limit: x86_power_limit,
}

#[repr(C)]
#[derive(Default, Debug, PartialEq, Copy, Clone)]
pub struct acpi_transition_s_state {
    target_s_state: u8, // Value between 1 and 5 indicating which S-state
    sleep_type_a: u8,   // Value from ACPI VM (SLP_TYPa)
    sleep_type_b: u8,   // Value from ACPI VM (SLP_TYPb)
    _padding1: [PadByte; 9],
}

#[repr(C)]
#[derive(Default, Debug, PartialEq, Copy, Clone)]
pub struct x86_power_limit {
    power_limit: u32, // PL1 value in milliwatts
    time_window: u32, // PL1 time window in microseconds
    clamp: u8,        // PL1 clamping enable
    enable: u8,       // PL1 enable
    _padding2: [PadByte; 2],
}

// source: zircon/system/public/zircon/syscalls/pci.h
pub type zx_pci_bar_types_t = u32;

multiconst!(zx_pci_bar_types_t, [
            ZX_PCI_BAR_TYPE_UNUSED = 0;
            ZX_PCI_BAR_TYPE_MMIO = 1;
            ZX_PCI_BAR_TYPE_PIO = 2;
]);

#[repr(C)]
pub struct zx_pci_bar_t {
    id: u32,
    ty: u32,
    size: usize,
    // rust can't express anonymous unions at this time
    // https://github.com/rust-lang/rust/issues/49804
    zx_pci_bar_union: zx_pci_bar_union,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_pci_bar_union {
    addr: usize,
    zx_pci_bar_union_struct: zx_pci_bar_union_struct,
}

#[repr(C)]
#[derive(Default, Debug, PartialEq, Copy, Clone)]
pub struct zx_pci_bar_union_struct {
    handle: zx_handle_t,
    _padding1: [PadByte; 4],
}

// source: zircon/system/public/zircon/syscalls/smc.h
#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_smc_parameters_t {
    func_id: u32,
    _padding1: [PadByte; 4],
    arg1: u64,
    arg2: u64,
    arg3: u64,
    arg4: u64,
    arg5: u64,
    arg6: u64,
    client_id: u16,
    secure_os_id: u16,
    _padding2: [PadByte; 4],
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_smc_result_t {
    arg0: u64,
    arg1: u64,
    arg2: u64,
    arg3: u64,
    arg6: u64,
}

const ZX_CPU_SET_MAX_CPUS: usize = 512;
const ZX_CPU_SET_BITS_PER_WORD: usize = 64;

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_cpu_set_t {
    mask: [u64; ZX_CPU_SET_MAX_CPUS / ZX_CPU_SET_BITS_PER_WORD],
}

// source: zircon/system/public/zircon/syscalls/scheduler.h
#[repr(C)]
#[derive(Copy, Clone)]
pub struct zx_profile_info_t {
    flags: u32,
    _padding1: [PadByte; 4],
    zx_profile_info_union: zx_profile_info_union,
    cpu_affinity_mask: zx_cpu_set_t,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct priority_params {
    priority: i32,
    _padding2: [PadByte; 20],
}

#[repr(C)]
#[derive(Copy, Clone)]
union zx_profile_info_union {
    priority_params: priority_params,
    deadline_params: zx_sched_deadline_params_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
struct zx_sched_deadline_params_t {
    capacity: zx_duration_t,
    relative_deadline: zx_duration_t,
    period: zx_duration_t,
}

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_cpu_performance_scale_t {
    pub integer_part: u32,
    pub fractional_part: u32,
}

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_cpu_performance_info_t {
    pub logical_cpu_number: u32,
    pub performance_scale: zx_cpu_performance_scale_t,
}

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_iommu_desc_dummy_t {
    pub reserved: u8,
}

multiconst!(u32, [
    ZX_IOMMU_TYPE_DUMMY = 0;
    ZX_IOMMU_TYPE_INTEL = 1;
]);

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn padded_struct_equality() {
        let test_struct = zx_clock_update_args_v1_t {
            rate_adjust: 222,
            padding1: [PadByte(0), PadByte(0), PadByte(0), PadByte(0)],
            value: 333,
            error_bound: 444,
        };

        let different_data = zx_clock_update_args_v1_t { rate_adjust: 999, ..test_struct.clone() };

        let different_padding = zx_clock_update_args_v1_t {
            padding1: [PadByte(0), PadByte(1), PadByte(2), PadByte(3)],
            ..test_struct.clone()
        };

        // Structures with different data should not be equal.
        assert_ne!(test_struct, different_data);
        // Structures with only different padding should not be equal.
        assert_eq!(test_struct, different_padding);
    }

    #[test]
    fn padded_struct_debug() {
        let test_struct = zx_clock_update_args_v1_t {
            rate_adjust: 222,
            padding1: [PadByte(0), PadByte(0), PadByte(0), PadByte(0)],
            value: 333,
            error_bound: 444,
        };
        let expectation = "zx_clock_update_args_v1_t { \
            rate_adjust: 222, \
            padding1: [-, -, -, -], \
            value: 333, \
            error_bound: 444 }";
        assert_eq!(format!("{:?}", test_struct), expectation);
    }
}
