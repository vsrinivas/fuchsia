// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

extern crate core;

#[macro_use]
extern crate bitflags;

pub type mx_handle_t = i32;

pub type mx_status_t = i32;

// Auto-generated using tools/gen_status.py
pub const NO_ERROR              : mx_status_t = 0;
pub const ERR_INTERNAL          : mx_status_t = -1;
pub const ERR_NOT_SUPPORTED     : mx_status_t = -2;
pub const ERR_NO_RESOURCES      : mx_status_t = -5;
pub const ERR_NO_MEMORY         : mx_status_t = -4;
pub const ERR_INVALID_ARGS      : mx_status_t = -10;
pub const ERR_WRONG_TYPE        : mx_status_t = -54;
pub const ERR_BAD_SYSCALL       : mx_status_t = -11;
pub const ERR_BAD_HANDLE        : mx_status_t = -12;
pub const ERR_OUT_OF_RANGE      : mx_status_t = -13;
pub const ERR_BUFFER_TOO_SMALL  : mx_status_t = -14;
pub const ERR_BAD_STATE         : mx_status_t = -20;
pub const ERR_NOT_FOUND         : mx_status_t = -3;
pub const ERR_ALREADY_EXISTS    : mx_status_t = -15;
pub const ERR_ALREADY_BOUND     : mx_status_t = -16;
pub const ERR_TIMED_OUT         : mx_status_t = -23;
pub const ERR_HANDLE_CLOSED     : mx_status_t = -24;
pub const ERR_REMOTE_CLOSED     : mx_status_t = -25;
pub const ERR_UNAVAILABLE       : mx_status_t = -26;
pub const ERR_SHOULD_WAIT       : mx_status_t = -27;
pub const ERR_ACCESS_DENIED     : mx_status_t = -30;
pub const ERR_IO                : mx_status_t = -40;
pub const ERR_IO_REFUSED        : mx_status_t = -41;
pub const ERR_IO_DATA_INTEGRITY : mx_status_t = -42;
pub const ERR_IO_DATA_LOSS      : mx_status_t = -43;
pub const ERR_BAD_PATH          : mx_status_t = -50;
pub const ERR_NOT_DIR           : mx_status_t = -51;
pub const ERR_NOT_FILE          : mx_status_t = -52;

pub type mx_time_t = u64;
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
        const MX_OBJECT_SIGNAL_22         = 1 << 22,
        const MX_OBJECT_SIGNAL_23         = 1 << 23,
        const MX_USER_SIGNAL_0            = 1 << 24,
        const MX_USER_SIGNAL_1            = 1 << 25,
        const MX_USER_SIGNAL_2            = 1 << 26,
        const MX_USER_SIGNAL_3            = 1 << 27,
        const MX_USER_SIGNAL_4            = 1 << 28,
        const MX_USER_SIGNAL_5            = 1 << 29,
        const MX_USER_SIGNAL_6            = 1 << 30,
        const MX_USER_SIGNAL_7            = 1 << 31,
        // Event
        const MX_EVENT_SIGNAL_SIGNALED    = MX_OBJECT_SIGNAL_3.bits,
        const MX_EVENT_SIGNAL_MASK        =
            (MX_USER_SIGNAL_ALL.bits | MX_OBJECT_SIGNAL_3.bits),
        // EventPair
        const MX_EPAIR_SIGNAL_SIGNALED    = MX_OBJECT_SIGNAL_3.bits,
        const MX_EPAIR_SIGNAL_CLOSED      = MX_OBJECT_SIGNAL_2.bits,
        const MX_EPAIR_SIGNAL_MASK        =
            (MX_USER_SIGNAL_ALL.bits | MX_OBJECT_SIGNAL_2.bits | MX_OBJECT_SIGNAL_3.bits),
        // Task signals
        const MX_TASK_SIGNAL_TERMINATED   = MX_OBJECT_SIGNAL_3.bits,
        const MX_TASK_SIGNAL_MASK         = MX_OBJECT_SIGNAL_3.bits,
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

// flags for channel creation
// TODO: this doesn't have a typedef in the public magenta interface, maybe it should
// be a named type anyway
pub const MX_CHANNEL_CREATE_REPLY_CHANNEL: u32 = 1 << 0;

// clock ids
pub const MX_CLOCK_MONOTONIC: u32 = 0;

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

#[link(name = "magenta")]
extern {

    // Randomness
    pub fn mx_cprng_draw(buffer: *mut u8, len: mx_size_t, actual: *mut mx_size_t)
        -> mx_status_t;
    pub fn mx_cprng_add_entropy(buffer: *const u8, len: mx_size_t) -> mx_status_t;

    // Time
    pub fn mx_time_get(clock_id: u32) -> mx_time_t;

    pub fn mx_nanosleep(nanoseconds: mx_time_t) -> mx_status_t;

    // Generic handle operations
    pub fn mx_handle_close(handle: mx_handle_t) -> mx_status_t;
    pub fn mx_handle_duplicate(handle: mx_handle_t, rights: mx_rights_t) -> mx_handle_t;
    pub fn mx_handle_wait_one(handle: mx_handle_t, signals: mx_signals_t, timeout: mx_time_t,
        pending: *mut mx_signals_t) -> mx_status_t;
    pub fn mx_handle_wait_many(items: *mut mx_wait_item_t, count: u32,
        timeout: mx_time_t) -> mx_status_t;
    pub fn mx_handle_replace(handle: mx_handle_t, rights: mx_rights_t) -> mx_handle_t;

    // Channels
    pub fn mx_channel_create(options: u32, out0: *mut mx_handle_t, out1: *mut mx_handle_t)
        -> mx_status_t;
    pub fn mx_channel_read(handle: mx_handle_t, options: u32, bytes: *mut u8,
        num_bytes: u32, actual_bytes: *mut u32, handles: *mut mx_handle_t,
        num_handles: u32, actual_handles: *mut u32) -> mx_status_t;
    pub fn mx_channel_write(handle: mx_handle_t, options: u32, bytes: *const u8,
        num_bytes: u32, handles: *const mx_handle_t, num_handles: u32) -> mx_status_t;

    // Wait sets
    pub fn mx_waitset_create(options: u32, out: *mut mx_handle_t) -> mx_status_t;
    pub fn mx_waitset_add(waitset_handle: mx_handle_t, cookie: u64, handle: mx_handle_t,
        signals: mx_signals_t) -> mx_status_t;
    pub fn mx_waitset_remove(waitset_handle: mx_handle_t, cookie: u64) -> mx_status_t;
    pub fn mx_waitset_wait(waitset_handle: mx_handle_t, timeout: mx_time_t,
        results: *mut mx_waitset_result_t, count: *mut u32) -> mx_status_t;

    // Virtual Memory Objects
    pub fn mx_vmo_create(size: u64, options: u32, out: *mut mx_handle_t) -> mx_status_t;
    pub fn mx_vmo_read(handle: mx_handle_t, data: *mut u8, offset: u64, len: mx_size_t,
        actual: *mut mx_size_t) -> mx_status_t;
    pub fn mx_vmo_write(handle: mx_handle_t, data: *const u8, offset: u64, len: mx_size_t,
        actual: *mut mx_size_t) -> mx_status_t;
    pub fn mx_vmo_get_size(handle: mx_handle_t, size: *mut u64) -> mx_status_t;
    pub fn mx_vmo_set_size(handle: mx_handle_t, size: u64) -> mx_status_t;
    // todo: vmo_op_range

}
