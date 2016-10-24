// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate core;

#[allow(non_camel_case_types)]
pub type mx_handle_t = i32;

#[allow(non_camel_case_types)]
pub type mx_status_t = i32;

pub const NO_ERROR: mx_status_t = 0;
pub const ERR_OUT_OF_RANGE: mx_status_t = -13;
pub const ERR_BUFFER_TOO_SMALL: mx_status_t = -14;
// TODO: lots of error definitions (probably auto-gen from fuchsia-types.def)

#[allow(non_camel_case_types)]
pub type mx_time_t = u64;
pub const MX_TIME_INFINITE : mx_time_t = core::u64::MAX;

// TODO: might want to use a proper bitfield type
#[allow(non_camel_case_types)]
pub type mx_signals_t = u32;
pub const MX_SIGNAL_NONE            : mx_signals_t = 0;
pub const MX_SIGNAL_READABLE        : mx_signals_t = 1 << 0;
pub const MX_SIGNAL_WRITABLE        : mx_signals_t = 1 << 1;
pub const MX_SIGNAL_PEER_CLOSED     : mx_signals_t = 1 << 2;
pub const MX_SIGNAL_SIGNAL0         : mx_signals_t = 1 << 3;
pub const MX_SIGNAL_SIGNAL1         : mx_signals_t = 1 << 4;
pub const MX_SIGNAL_SIGNAL2         : mx_signals_t = 1 << 5;
pub const MX_SIGNAL_SIGNAL3         : mx_signals_t = 1 << 6;
pub const MX_SIGNAL_SIGNAL4         : mx_signals_t = 1 << 7;
pub const MX_SIGNAL_SIGNAL_ALL      : mx_signals_t = 31 << 3;
pub const MX_SIGNAL_READ_THRESHOLD  : mx_signals_t = 1 << 8;
pub const MX_SIGNAL_WRITE_THRESHOLD : mx_signals_t = 1 << 9;
pub const MX_SIGNAL_SIGNALED        : mx_signals_t = MX_SIGNAL_SIGNAL0;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct mx_signals_state_t {
    pub satisfied: mx_signals_t,
    pub satisfiable: mx_signals_t,
}

#[allow(non_camel_case_types)]
pub type mx_size_t = usize;
#[allow(non_camel_case_types)]
pub type mx_ssize_t = isize;

// TODO: might want to use a proper bitfield type
#[allow(non_camel_case_types)]
pub type mx_rights_t = u32;
pub const MX_RIGHT_NONE         : mx_rights_t = 0;
pub const MX_RIGHT_DUPLICATE    : mx_rights_t = 1 << 0;
pub const MX_RIGHT_TRANSFER     : mx_rights_t = 1 << 1;
pub const MX_RIGHT_READ         : mx_rights_t = 1 << 2;
pub const MX_RIGHT_WRITE        : mx_rights_t = 1 << 3;
pub const MX_RIGHT_EXECUTE      : mx_rights_t = 1 << 4;
pub const MX_RIGHT_MAP          : mx_rights_t = 1 << 5;
pub const MX_RIGHT_GET_PROPERTY : mx_rights_t = 1 << 6;
pub const MX_RIGHT_SET_PROPERTY : mx_rights_t = 1 << 7;
pub const MX_RIGHT_DEBUG        : mx_rights_t = 1 << 8;
pub const MX_RIGHT_SAME_RIGHTS  : mx_rights_t = 1 << 31;

// flags for message pipe creation
// TODO: this doesn't have a typedef in the public magenta interface, maybe it should
// be a named type anyway
pub const MX_FLAG_REPLY_PIPE: u32 = 1 << 0;

#[repr(C)]
pub struct mx_waitset_result_t {
    pub cookie: u64,
    pub wait_result: mx_status_t,
    _reserved: u32,
    pub signals_state: mx_signals_state_t,
}

#[link(name = "magenta")]
extern {

    // Randomness
    pub fn mx_cprng_draw(buffer: *mut u8, len: mx_size_t) -> mx_ssize_t;
    pub fn mx_cprng_add_entropy(buffer: *const u8, len: mx_size_t) -> mx_status_t;

    // Time
    pub fn mx_current_time() -> mx_time_t;

    pub fn mx_nanosleep(nanoseconds: mx_time_t) -> mx_status_t;

    // Generic handle operations
    pub fn mx_handle_close(handle: mx_handle_t) -> mx_status_t;
    pub fn mx_handle_duplicate(handle: mx_handle_t, right: mx_rights_t) -> mx_handle_t;
    pub fn mx_handle_wait_one(handle: mx_handle_t, signals: mx_signals_t, timeout: mx_time_t,
        signals_state: *mut mx_signals_state_t) -> mx_status_t;
    pub fn mx_handle_wait_many(count: u32, handles: *const mx_handle_t,
        signals: *const mx_signals_t, timeout: mx_time_t, result_index: *mut u32,
        signals_states: *mut mx_signals_state_t) -> mx_status_t;
    pub fn mx_handle_replace(handle: mx_handle_t, rights: mx_rights_t) -> mx_handle_t;

    // Message pipes
    pub fn mx_msgpipe_create(out_handles: *mut mx_handle_t, flags: u32) -> mx_status_t;
    pub fn mx_msgpipe_read(handle: mx_handle_t, bytes: *mut u8, num_bytes: *mut u32,
        handles: *mut mx_handle_t, num_handles: *mut u32, flags: u32) -> mx_status_t;
    pub fn mx_msgpipe_write(handle: mx_handle_t, bytes: *const u8, num_bytes: u32,
        handles: *const mx_handle_t, num_handles: u32, flags: u32) -> mx_status_t;

    // Wait sets
    pub fn mx_waitset_create() -> mx_status_t;
    pub fn mx_waitset_add(waitset_handle: mx_handle_t, handle: mx_handle_t,
        signals: mx_signals_t, cookie: u64) -> mx_status_t;
    pub fn mx_waitset_remove(waitset_handle: mx_handle_t, cookie: u64) -> mx_status_t;
    pub fn mx_waitset_wait(waitset_handle: mx_handle_t, timeout: mx_time_t,
        num_results: *mut u32, results: *mut mx_waitset_result_t,
        max_results: *mut u32) -> mx_status_t;

    // Virtual Memory Objects
    pub fn mx_vmo_create(size: u64) -> mx_handle_t;
    pub fn mx_vmo_read(handle: mx_handle_t, data: *mut u8, offset: u64, len: mx_size_t)
        -> mx_ssize_t;
    pub fn mx_vmo_write(handle: mx_handle_t, data: *const u8, offset: u64, len: mx_size_t)
        -> mx_ssize_t;
    pub fn mx_vmo_get_size(handle: mx_handle_t, size: *mut u64) -> mx_status_t;
    pub fn mx_vmo_set_size(handle: mx_handle_t, size: u64) -> mx_status_t;
    // todo: vmo_op_range

}
