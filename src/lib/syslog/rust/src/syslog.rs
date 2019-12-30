// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::sys::*;
use std::os::raw::c_char;

#[must_use = "pointers to this type should never be dereferenced"]
pub struct fx_logger_t {}

pub type fx_log_severity_t = i32;

#[repr(C)]
pub struct fx_logger_config_t {
    pub severity: fx_log_severity_t,
    pub fd: i32,
    pub log_service_channel: zx_handle_t,
    pub tags: *const *const c_char,
    pub num_tags: usize,
}

pub const FX_LOG_INFO: fx_log_severity_t = 0;
pub const FX_LOG_WARN: fx_log_severity_t = 1;
pub const FX_LOG_ERROR: fx_log_severity_t = 2;

#[link(name = "syslog")]
#[allow(improper_ctypes)]
extern "C" {
    pub fn fx_log_init() -> zx_status_t;

    pub fn fx_log_init_with_config(config: *const fx_logger_config_t) -> zx_status_t;

    pub fn fx_log_get_logger() -> *mut fx_logger_t;

    pub fn fx_logger_get_min_severity(logger: *mut fx_logger_t) -> fx_log_severity_t;

    pub fn fx_logger_set_min_severity(logger: *mut fx_logger_t, severity: fx_log_severity_t);

    pub fn fx_logger_log(
        logger: *mut fx_logger_t,
        severity: fx_log_severity_t,
        tag: *const c_char,
        msg: *const c_char,
    ) -> zx_status_t;
}
