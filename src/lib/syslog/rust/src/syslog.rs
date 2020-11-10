// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_logger as flogger;
use fuchsia_zircon::sys::*;
use std::os::raw::c_char;

#[must_use = "pointers to this type should never be dereferenced"]
pub struct fx_logger_t {}

pub type fx_log_severity_t = i32;

#[repr(C)]
pub struct fx_logger_config_t {
    pub severity: fx_log_severity_t,
    pub fd: i32,
    pub log_sink_channel: zx_handle_t,
    pub log_sink_socket: zx_handle_t,
    pub log_service_channel: zx_handle_t,
    pub tags: *const *const c_char,
    pub num_tags: usize,
}

// Constants defined via logger.fidl
pub const FX_LOG_ALL: fx_log_severity_t = flogger::LogLevelFilter::All as i32;
pub const FX_LOG_TRACE: fx_log_severity_t = flogger::LogLevelFilter::Trace as i32;
pub const FX_LOG_DEBUG: fx_log_severity_t = flogger::LogLevelFilter::Debug as i32;
pub const FX_LOG_INFO: fx_log_severity_t = flogger::LogLevelFilter::Info as i32;
pub const FX_LOG_WARN: fx_log_severity_t = flogger::LogLevelFilter::Warn as i32;
pub const FX_LOG_ERROR: fx_log_severity_t = flogger::LogLevelFilter::Error as i32;
pub const FX_LOG_FATAL: fx_log_severity_t = flogger::LogLevelFilter::Fatal as i32;

pub const FX_LOG_VERBOSITY_STEP_SIZE: i32 = flogger::LOG_VERBOSITY_STEP_SIZE as i32;
pub const FX_LOG_SEVERITY_DEFAULT: fx_log_severity_t = flogger::LOG_LEVEL_DEFAULT as i32;

#[link(name = "syslog")]
#[allow(improper_ctypes)]
extern "C" {
    pub fn fx_log_reconfigure(config: *const fx_logger_config_t) -> zx_status_t;

    pub fn fx_log_get_logger() -> *mut fx_logger_t;

    pub fn fx_logger_get_min_severity(logger: *mut fx_logger_t) -> fx_log_severity_t;

    pub fn fx_logger_set_min_severity(logger: *mut fx_logger_t, severity: fx_log_severity_t);

    pub fn fx_logger_log(
        logger: *mut fx_logger_t,
        severity: fx_log_severity_t,
        tag: *const c_char,
        msg: *const c_char,
    ) -> zx_status_t;

    pub fn fx_logger_create(
        config: *const fx_logger_config_t,
        logger_ptr: *const *mut fx_logger_t,
    ) -> zx_status_t;

    pub fn fx_logger_destroy(logger: *const fx_logger_t);
    pub fn fx_logger_get_connection_status(logger: *const fx_logger_t) -> zx_status_t;
}
