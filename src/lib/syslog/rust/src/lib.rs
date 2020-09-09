//! Rust fuchsia logger library.

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(missing_docs)]

use fuchsia_zircon::{self as zx, sys::*, HandleBased};
use lazy_static::lazy_static;
use log::{Level, LevelFilter, Metadata, Record};
use std::ffi::{CStr, CString};
use std::fmt::Arguments;
use std::os::raw::c_char;
use std::panic;

#[allow(non_camel_case_types)]
mod syslog;

/// Encapsulates Log Levels.
pub mod levels {
    use crate::syslog;

    /// Defines log levels for clients.
    pub type LogLevel = i32;

    /// ALL log level
    pub const ALL: LogLevel = syslog::FX_LOG_ALL;

    /// TRACE log level
    pub const TRACE: LogLevel = syslog::FX_LOG_TRACE;

    /// DEBUG log level
    pub const DEBUG: LogLevel = syslog::FX_LOG_DEBUG;

    /// INFO log level
    pub const INFO: LogLevel = syslog::FX_LOG_INFO;

    /// WARN log level
    pub const WARN: LogLevel = syslog::FX_LOG_WARN;

    /// ERROR log level
    pub const ERROR: LogLevel = syslog::FX_LOG_ERROR;

    /// FATAL log level
    pub const FATAL: LogLevel = syslog::FX_LOG_FATAL;
}

/// Convenient re-export of macros for globed imports Rust Edition 2018
pub mod macros {
    pub use crate::fx_log;
    pub use crate::fx_log_debug;
    pub use crate::fx_log_err;
    pub use crate::fx_log_info;
    pub use crate::fx_log_trace;
    pub use crate::fx_log_warn;
}

/// Maps log crate log levels to syslog severity levels.
fn get_fx_logger_severity(level: Level) -> syslog::fx_log_severity_t {
    match level {
        Level::Trace => syslog::FX_LOG_TRACE,
        Level::Debug => syslog::FX_LOG_DEBUG,
        Level::Info => syslog::FX_LOG_INFO,
        Level::Warn => syslog::FX_LOG_WARN,
        Level::Error => syslog::FX_LOG_ERROR,
    }
}

/// Maps syslog severity levels to  log crate log filters.
fn get_log_filter(level: levels::LogLevel) -> LevelFilter {
    match level {
        syslog::FX_LOG_ALL => LevelFilter::Trace, // log::LevelFilter fidelity
        syslog::FX_LOG_TRACE => LevelFilter::Trace,
        syslog::FX_LOG_DEBUG => LevelFilter::Debug,
        syslog::FX_LOG_INFO => LevelFilter::Info,
        syslog::FX_LOG_WARN => LevelFilter::Warn,
        syslog::FX_LOG_ERROR => LevelFilter::Error,
        syslog::FX_LOG_FATAL => LevelFilter::Error, // log::LevelFilter fidelity
        _ => LevelFilter::Off,                      // return for all other levels
    }
}

/// Convenience macro for logging.
///
/// Example:
///
/// ```rust
/// fx_log!(tag: "my_tag", levels::WARN, "print integer {}", 10);
/// fx_log!(levels::WARN, "print integer {}", 10);
/// ```
#[macro_export]
macro_rules! fx_log {
    (tag: $tag:expr, $lvl:expr, $($arg:tt)+) => ({
        let lvl = $lvl;
        if $crate::LOGGER.is_enabled(lvl) {
            $crate::log_helper(format_args!($($arg)+), lvl, $tag);
        }
    });
    ($lvl:expr, $($arg:tt)+) => ($crate::fx_log!(tag: "", $lvl, $($arg)+))
}

/// Convenience macro to log error.
///
/// Example:
///
/// ```rust
/// fx_log_err!(tag: "my_tag", "failed: {}", e);
/// fx_log_err!("failed: {}", e);
/// ```
#[macro_export]
macro_rules! fx_log_err {
    (tag: $tag:expr, $($arg:tt)*) => (
        $crate::fx_log!(tag: $tag, $crate::levels::ERROR, "[{}({})] {}",
        file!().trim_start_matches("../"), line!(), format_args!($($arg)*));
    );
    ($($arg:tt)*) => (
        $crate::fx_log_err!(tag: "", $($arg)*);
    )
}

/// Convenience macro to log warning.
///
/// Example:
///
/// ```rust
/// fx_log_warn!(tag: "my_tag", "print integer {}", 10);
/// fx_log_warn!("print integer {}", 10);
/// ```
#[macro_export]
macro_rules! fx_log_warn {
    (tag: $tag:expr, $($arg:tt)*) => (
        $crate::fx_log!(tag: $tag, $crate::levels::WARN, $($arg)*);
    );
    ($($arg:tt)*) => (
        $crate::fx_log_warn!(tag: "", $($arg)*);
    )
}

/// Convenience macro to log information.
///
/// Example:
///
/// ```rust
/// fx_log_info!(tag: "my_tag", "print integer {}", 10);
/// fx_log_info!("print integer {}", 10);
/// ```
#[macro_export]
macro_rules! fx_log_info {
    (tag: $tag:expr, $($arg:tt)*) => (
        $crate::fx_log!(tag: $tag, $crate::levels::INFO, $($arg)*);
    );
    ($($arg:tt)*) => (
        $crate::fx_log_info!(tag: "", $($arg)*);
    )
}

/// Convenience macro to log debug info.
///
/// Example:
///
/// ```rust
/// fx_log_debug!(tag: "my_tag", "print integer {}", 10);
/// fx_log_debug!("print integer {}", 10);
/// ```
#[macro_export]
macro_rules! fx_log_debug {
    (tag: $tag:expr, $($arg:tt)*) => (
        $crate::fx_log!(tag: $tag, $crate::levels::DEBUG, $($arg)*);
    );
    ($($arg:tt)*) => (
        $crate::fx_log_debug!(tag: "", $($arg)*);
    )
}

/// Convenience macro to log trace info.
///
/// Example:
///
/// ```rust
/// fx_log_trace!(tag: "my_tag", "print integer {}", 10);
/// fx_log_trace!("print integer {}", 10);
/// ```
#[macro_export]
macro_rules! fx_log_trace {
    (tag: $tag:expr, $($arg:tt)*) => (
        $crate::fx_log!(tag: $tag, $crate::levels::TRACE, $($arg)*);
    );
    ($($arg:tt)*) => (
        $crate::fx_log_trace!(tag: "", $($arg)*);
    )
}

/// Convenience macro to log verbose messages.
///
/// Example:
///
/// ```rust
/// fx_vlog!(tag: "my_tag", 1 /*verbosity*/, "print integer {}", 10);
/// fx_vlog!(2 /*verbosity*/, "print integer {}", 10);
/// ```
#[macro_export]
macro_rules! fx_vlog {
    (tag: $tag:expr, $verbosity:expr, $($arg:tt)*) => (
        $crate::fx_log!(tag: $tag, $crate::get_severity_from_verbosity($verbosity), $($arg)*);
    );
    ($verbosity:expr, $($arg:tt)*) => (
         $crate::fx_vlog!(tag: "", $verbosity, $($arg)*);
    )
}

/// C API logger wrapper which provides wrapper for C APIs.
pub struct Logger {
    logger: *mut syslog::fx_logger_t,
}

impl Logger {
    /// Wrapper around C API `fx_logger_get_min_severity`.
    fn get_severity(&self) -> syslog::fx_log_severity_t {
        unsafe { syslog::fx_logger_get_min_severity(self.logger) }
    }

    /// Returns true if inner logger is not null and log level is enabled.
    pub fn is_enabled(&self, severity: levels::LogLevel) -> bool {
        if !self.logger.is_null() {
            return self.get_severity() <= severity;
        }
        false
    }

    #[doc(hidden)]
    /// Wrapper around C API `fx_logger_log`.
    pub fn log_c(
        &self,
        severity: syslog::fx_log_severity_t,
        tag: &CStr,
        msg: &CStr,
    ) -> zx_status_t {
        unsafe { syslog::fx_logger_log(self.logger, severity, tag.as_ptr(), msg.as_ptr()) }
    }

    /// Set logger severity. Returns false if internal logger is null.
    pub fn set_severity(&self, severity: levels::LogLevel) -> bool {
        if !self.logger.is_null() {
            unsafe { syslog::fx_logger_set_min_severity(self.logger, severity) };
            return true;
        }
        false
    }
}

lazy_static! {
    /// Global reference to default logger object.
    pub static ref LOGGER: Logger = {
        let l = get_default();
        install_panic_hook();
        l
    };
}

/// macro helper function to convert strings and call log
pub fn log_helper(args: Arguments<'_>, lvl: i32, tag: &str) {
    let s = std::fmt::format(args);
    let c_msg = CString::new(s).unwrap();
    let c_tag = CString::new(tag).unwrap();
    LOGGER.log_c(lvl, &c_tag, &c_msg);
}

/// Gets default logger.
fn get_default() -> Logger {
    Logger { logger: unsafe { syslog::fx_log_get_logger() } }
}

unsafe impl Send for Logger {}

unsafe impl Sync for Logger {}

impl log::Log for Logger {
    fn enabled(&self, metadata: &Metadata<'_>) -> bool {
        self.is_enabled(get_fx_logger_severity(metadata.level()))
    }

    fn log(&self, record: &Record<'_>) {
        if record.level() == Level::Error {
            fx_log!(tag:record.target(),
                get_fx_logger_severity(record.level()), "[{}({})] {}",
                record.file().unwrap_or("??").trim_start_matches("../"), record.line().unwrap_or(0), record.args());
        } else {
            fx_log!(tag:record.target(),
                get_fx_logger_severity(record.level()), "{}", record.args());
        }
    }

    fn flush(&self) {}
}

/// Initializes syslogger using default options.
pub fn init() -> Result<(), zx::Status> {
    init_with_tags_and_handle(zx::sys::ZX_HANDLE_INVALID, &[])
}

/// Initializes syslogger with tags. Max number of tags can be 4
/// and max length of each tag can be 63 characters.
pub fn init_with_tags(tags: &[&str]) -> Result<(), zx::Status> {
    init_with_tags_and_handle(zx::sys::ZX_HANDLE_INVALID, tags)
}

/// Initialize syslogger with a single tag and a log service channel socket.
pub fn init_with_socket_and_name(sink: zx::Socket, name: &str) -> Result<(), zx::Status> {
    init_with_tags_and_handle(sink.into_raw(), &[name])
}

/// Initializes syslogger with tags. Max number of tags can be 4
/// and max length of each tag can be 63 characters.
fn init_with_tags_and_handle(handle: zx_handle_t, tags: &[&str]) -> Result<(), zx::Status> {
    let cstr_vec: Vec<CString> = tags
        .iter()
        .map(|x| CString::new(x.to_owned()).expect("Cannot create tag with interior null"))
        .collect();
    let c_tags: Vec<*const c_char> = cstr_vec.iter().map(|x| x.as_ptr()).collect();
    let config = syslog::fx_logger_config_t {
        severity: syslog::FX_LOG_SEVERITY_DEFAULT,
        fd: -1,
        log_service_channel: handle,
        tags: c_tags.as_ptr(),
        num_tags: c_tags.len(),
    };
    let status = unsafe { syslog::fx_log_reconfigure(&config) };
    if status == zx::Status::OK.into_raw() {
        log::set_logger(&*LOGGER).expect("Attempted to initialize multiple loggers");
        log::set_max_level(get_log_filter(config.severity));
    }
    zx::ok(status)
}

/// Installs a new panic hook to send the panic message to the log service, since v2 components
/// won't have stdout.
fn install_panic_hook() {
    let default_hook = panic::take_hook();
    panic::set_hook(Box::new(move |panic_info| {
        // Handle common cases of &'static str or String payload.
        let msg = match panic_info.payload().downcast_ref::<&'static str>() {
            Some(s) => *s,
            None => match panic_info.payload().downcast_ref::<String>() {
                Some(s) => &s[..],
                None => "<Unknown panic payload type>",
            },
        };

        fx_log!(tag: "", levels::ERROR, "{}", format_args!("PANIC: {}", msg));

        default_hook(panic_info);
    }));
}

/// Set default logger severity.
pub fn set_severity(severity: levels::LogLevel) {
    if LOGGER.set_severity(severity) {
        log::set_max_level(get_log_filter(severity));
    }
}

/// Get the severity corresponding to the given verbosity. Note that
/// verbosity relative to the default severity and can be thought of
/// as incrementally "more vebose than" the baseline.
pub fn get_severity_from_verbosity(mut verbosity: i32) -> i32 {
    verbosity = std::cmp::max(0, verbosity);

    // verbosity scale sits in the interstitial space between INFO and DEBUG
    std::cmp::max(
        syslog::FX_LOG_DEBUG + 1,
        syslog::FX_LOG_INFO - (verbosity * syslog::FX_LOG_VERBOSITY_STEP_SIZE),
    )
}

/// Set default logger verbosity.
#[inline]
pub fn set_verbosity(verbosity: u16) {
    set_severity(get_severity_from_verbosity(verbosity as i32));
}

/// Checks if default logger is enabled for given log level.
pub fn is_enabled(severity: levels::LogLevel) -> bool {
    LOGGER.is_enabled(severity)
}

#[cfg(test)]
mod test {
    use super::*;

    use log::{debug, error, info, trace, warn};
    use std::fs::File;
    use std::io::Read;
    use std::os::unix::io::AsRawFd;
    use std::ptr;
    use tempfile::TempDir;

    #[test]
    fn test() {
        let tmp_dir = TempDir::new().expect("should have created tempdir");
        let file_path = tmp_dir.path().join("tmp_file");
        let tmp_file = File::create(&file_path).expect("should have created file");
        let config = syslog::fx_logger_config_t {
            severity: levels::INFO,
            fd: tmp_file.as_raw_fd(),
            log_service_channel: zx::sys::ZX_HANDLE_INVALID,
            tags: ptr::null(),
            num_tags: 0,
        };
        let status = unsafe { syslog::fx_log_reconfigure(&config) };
        assert_eq!(status, zx::Status::OK.into_raw());

        fx_log_info!("info msg {}", 10);
        let mut expected: Vec<String> = vec![String::from("[] INFO: info msg 10")];

        fx_log_warn!("warn msg {}", 10);
        expected.push(String::from("[] WARNING: warn msg 10"));

        fx_log_err!("err msg {}", 10);
        let line = line!() - 1;
        expected.push(format!(
            "[] ERROR: [{}({})] err msg 10",
            file!().trim_start_matches("../"),
            line
        ));

        fx_log_info!(tag:"info_tag", "info msg {}", 10);
        expected.push(String::from("[info_tag] INFO: info msg 10"));

        fx_log_warn!(tag:"warn_tag", "warn msg {}", 10);
        expected.push(String::from("[warn_tag] WARNING: warn msg 10"));

        fx_log_err!(tag:"err_tag", "err msg {}", 10);
        let line = line!() - 1;
        expected.push(format!(
            "[err_tag] ERROR: [{}({})] err msg 10",
            file!().trim_start_matches("../"),
            line
        ));

        //test verbosity
        fx_vlog!(1, "verbose msg {}", 10); // will not log
        fx_vlog!(tag:"v_tag", 1, "verbose msg {}", 10); // will not log

        set_verbosity(1);
        fx_vlog!(1, "verbose2 msg {}", 10);
        expected.push(String::from("[] VLOG(1): verbose2 msg 10"));

        fx_vlog!(tag:"v_tag", 1, "verbose2 msg {}", 10);
        expected.push(String::from("[v_tag] VLOG(1): verbose2 msg 10"));

        // test log crate
        log::set_logger(&*LOGGER).expect("Attempted to initialize multiple loggers");

        set_severity(levels::DEBUG);
        info!("log info: {}", 10);
        let tag = "fuchsia_syslog_lib_test::test";
        expected.push(format!("[{}] INFO: log info: 10", tag));

        warn!("log warn: {}", 10);
        expected.push(format!("[{}] WARNING: log warn: 10", tag));

        error!("log err: {}", 10);
        let line = line!() - 1;
        expected.push(format!(
            "[{}] ERROR: [{}({})] log err: 10",
            tag,
            file!().trim_start_matches("../"),
            line
        ));

        debug!("log debug: {}", 10);
        expected.push(format!("[{}] DEBUG: log debug: 10", tag));

        trace!("log trace: {}", 10); // will not log

        set_severity(levels::TRACE);
        trace!("log trace2: {}", 10);
        expected.push(format!("[{}] TRACE: log trace2: 10", tag));

        // test set_severity
        set_severity(levels::WARN);
        info!("log info, will not log: {}", 10);
        warn!("log warn, will log: {}", 10);
        expected.push(format!("[{}] WARNING: log warn, will log: 10", tag));

        let mut tmp_file = File::open(&file_path).expect("should have opened the file");
        let mut content = String::new();
        tmp_file.read_to_string(&mut content).expect("something went wrong reading the file");
        let msgs = content.split("\n");
        let mut i = 0;
        for msg in msgs {
            if msg == "" {
                // last line - blank message
                continue;
            }
            if expected.len() <= i {
                panic!("Got extra line in msg \"{}\", full content\n{}", msg, content);
            } else if !msg.ends_with(&expected[i]) {
                panic!(
                    "expected msg:\n\"{}\"\nto end with\n\"{}\"\nfull content\n{}",
                    msg, expected[i], content
                );
            }
            i = i + 1;
        }
        if expected.len() != i {
            panic!("expected msgs:\n{:?}\nfull content\n{}", expected, content);
        }
    }
}
