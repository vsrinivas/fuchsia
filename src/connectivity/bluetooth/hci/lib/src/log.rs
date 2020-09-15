// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Macros to format and log to stderr.
//!
//! These macros contain the following limitations over the klog macros found in zircon:
//! 1) Severity is hardcoded in this file and it must be updated in source to change severity
//! 2) All messages end up logged at INFO level in klog. If the driver is not set to emit INFO
//!    logs, none of the log messages will be emitted. An additional label is prepended to the log
//!    message with the intended severity, but it is baked into the text of the message rather than
//!    being metadata that klog operates on.
//! 3) These are print statements to stderr. The behavior of devhost is to batch stderr output.
//!    Therefore, there is no guarantee of ordering of these messages with respect to messages
//!    coming from other drivers in the system. This batching happens at small subsecond intervals
//!    and intradriver log messages from a single thread are still guaranteed to be ordered.
//!
//! It is still better than using eprintln directly because the intent of a developer can be used
//! in the future to migrate to alternatives.

pub const SEVERITY_SPEW: u64 = 0;
pub const SEVERITY_TRACE: u64 = 1;
pub const SEVERITY_INFO: u64 = 2;
pub const SEVERITY_WARN: u64 = 3;
pub const SEVERITY_ERROR: u64 = 4;

/// Change to set minimum severity
pub const LOG_SEVERITY: u64 = SEVERITY_INFO;

/// Log a message to stderr, prepending it with invocation location and severity
macro_rules! bt_log {
    ($lvl:expr, $($arg:tt)+) => ({
        eprintln!("bt-transport [{}:{}:{}] {}",
            file!().split('/').next_back().unwrap_or("<unknown>"), line!(), $lvl,
            format_args!($($arg)+))
    });
}

/// Log a message at ERROR severity to stderr
#[macro_export]
macro_rules! bt_log_err {
    ($($arg:tt)+) => (if LOG_SEVERITY <= SEVERITY_ERROR { bt_log!("ERROR", $($arg)+) });
}

/// Log a message at WARN severity to stderr
#[macro_export]
macro_rules! bt_log_warn {
    ($($arg:tt)+) => (if LOG_SEVERITY <= SEVERITY_WARN { bt_log!("WARN", $($arg)+) });
}

/// Log a message at INFO severity to stderr
#[macro_export]
macro_rules! bt_log_info {
    ($($arg:tt)+) => (if LOG_SEVERITY <= SEVERITY_INFO { bt_log!("INFO", $($arg)+) });
}

/// Log a message at TRACE severity to stderr
#[macro_export]
macro_rules! bt_log_trace {
    ($($arg:tt)+) => (if LOG_SEVERITY <= SEVERITY_TRACE { bt_log!("TRACE", $($arg)+) });
}

/// Log a message at SPEW severity to stderr
#[macro_export]
macro_rules! bt_log_spew {
    ($($arg:tt)+) => (if LOG_SEVERITY <= SEVERITY_SPEW { bt_log!("SPEW", $($arg)+) });
}

/// Invoke the fuchsia_trace::duration! macro with the "bluetooth" category.
#[macro_export]
macro_rules! trace_duration {
    ($name:expr $(, $key:expr => $val:expr)*) => {
        ::fuchsia_trace::duration!("bluetooth", $name $(,$key => $val)*);
    }
}

/// Invoke the fuchsia_trace::instant! macro with the "bluetooth" category.
#[macro_export]
macro_rules! trace_instant {
    ($name:expr, $scope:expr $(, $key:expr => $val:expr)*) => {
        ::fuchsia_trace::instant!("bluetooth", $name, $scope $(,$key => $val)*);
    }
}

/// Invoke the fuchsia_trace::flow_begin! macro with the "bluetooth" category.
#[macro_export]
macro_rules! trace_flow_begin {
    ($name:expr, $flow_id:expr $(, $key:expr => $val:expr)*) => {
        ::fuchsia_trace::flow_begin!("bluetooth", $name, $flow_id $(,$key => $val)*);
    }
}

/// Invoke the fuchsia_trace::flow_end! macro with the "bluetooth" category.
#[macro_export]
macro_rules! trace_flow_end {
    ($name:expr, $flow_id:expr $(, $key:expr => $val:expr)*) => {
        ::fuchsia_trace::flow_end!("bluetooth", $name, $flow_id $(,$key => $val)*);
    }
}
