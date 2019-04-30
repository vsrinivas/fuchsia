// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon as zx, lazy_static::lazy_static, std::ffi::CString};

lazy_static! {
    static ref DEBUGLOG: Option<zx::sys::zx_handle_t> = {
        let mut log_handle = zx::sys::ZX_HANDLE_INVALID;
        let status = unsafe {
            zx::sys::zx_debuglog_create(
                zx::sys::ZX_HANDLE_INVALID,
                0,
                &mut log_handle as *mut zx::sys::zx_handle_t,
            )
        };
        if status != zx::sys::ZX_OK {
            eprintln!(
                "[component_manager] WARN: failed to create debuglog, falling back to stderr: {}",
                status
            );
            None
        } else {
            Some(log_handle)
        }
    };
}

/// `log_info` will write the given log message to zircon's debuglog, or to stderr if debuglog
/// creation fails. Arguments accepted are identical to that of `format!` or `println!`. The log
/// will be annotated with the string "INFO".
///
/// Example:
///
/// ```
/// log_info!("foobar: {:?}", current_state);
/// ```
macro_rules! log_info {
    ($($arg:tt)+) => {
        log_helper(log_format!("INFO", $($arg)+));
    }
}

/// `log_warn` will write the given log message to zircon's debuglog, or to stderr if debuglog
/// creation fails. Arguments accepted are identical to that of `format!` or `println!`. The log
/// will be annotated with the string "WARN".
///
/// Example:
///
/// ```
/// log_warn!("foobar: {:?}", current_state);
/// ```
macro_rules! log_warn {
    ($($arg:tt)+) => {
        log_helper(log_format!("WARN", $($arg)+));
    }
}

/// `log_error` will write the given log message to zircon's debuglog, or to stderr if debuglog
/// creation fails. Arguments accepted are identical to that of `format!` or `println!`. The log
/// will be annotated with the string "ERROR".
///
/// Example:
///
/// ```
/// log_error!("foobar: {:?}", current_state);
/// ```
macro_rules! log_error {
    ($($arg:tt)+) => {
        log_helper(log_format!("ERROR", $($arg)+));
    }
}

/// `log_fatal` will write the given log message to zircon's debuglog, or to stderr if debuglog
/// creation fails. Arguments accepted are identical to that of `format!` or `println!`. The log
/// will be annotated with the string "FATAL". After logging occurs, a panic will be triggered with
/// the same message that was logged.
///
/// Example:
///
/// ```
/// log_fatal!("foobar: {:?}", current_state);
/// ```
#[allow(unused_macros)]
macro_rules! log_fatal {
    ($($arg:tt)+) => ({
        let msg = log_format!("FATAL", $($arg)+);
        log_helper(msg.clone());
        panic!("{}", msg);
    })
}

/// `log_helper` is for internal use only. It is marked as `pub` because it is utilized by the
/// `log!` macro.
pub fn log_helper(msg: String) {
    if let Some(log_handle) = *DEBUGLOG {
        let c_msg = CString::new(msg).expect("failed to create c string");
        let status = unsafe {
            zx::sys::zx_debuglog_write(
                log_handle,
                0,
                c_msg.as_ptr() as *const u8,
                c_msg.as_bytes().len(),
            )
        };
        if status != zx::sys::ZX_OK {
            eprintln!(
                "{}",
                format!("failed to write log ({}): {}", status, c_msg.into_string().unwrap())
            );
        }
    } else {
        eprintln!("{}", msg);
    }
}

/// `log_format` is for internal use only.
macro_rules! log_format {
    ($lvl: expr, $($arg:tt)+) => {
        format!("[component_manager] {}: {}", $lvl, format_args!($($arg)+))
    };
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        byteorder::{ByteOrder, LittleEndian},
        rand::Rng,
        std::{panic, thread, time},
    };

    const ZX_LOG_FLAG_READABLE: u32 = 0x40000000;

    // expect_message_in_debuglog will read the first 10000 messages in zircon's debuglog, looking
    // for a message that equals `sent_msg`. If found, the function returns. If the first 10,000
    // messages doesn't contain `sent_msg`, it will panic.
    fn expect_message_in_debuglog(sent_msg: String) {
        let mut log_handle_for_reading = zx::sys::ZX_HANDLE_INVALID;
        let status = unsafe {
            zx::sys::zx_debuglog_create(
                zx::sys::ZX_HANDLE_INVALID,
                ZX_LOG_FLAG_READABLE,
                &mut log_handle_for_reading as *mut zx::sys::zx_handle_t,
            )
        };
        assert_eq!(zx::sys::ZX_OK, status);

        for _ in 0..10000 {
            let mut read_buffer = [0; 1024];
            let status = unsafe {
                zx::sys::zx_debuglog_read(log_handle_for_reading, 0, read_buffer.as_mut_ptr(), 1024)
            };
            if status <= 0 {
                if status == zx::sys::ZX_ERR_SHOULD_WAIT {
                    thread::sleep(time::Duration::from_millis(100));
                    continue;
                }
                assert_eq!(zx::sys::ZX_OK, status);
            }

            let data_len = LittleEndian::read_u16(&read_buffer[4..8]) as usize;
            let log = String::from_utf8(read_buffer[32..(32 + data_len)].to_vec())
                .expect("failed to read log buffer");
            if log == sent_msg {
                // We found our log!
                return;
            }
        }
        panic!("first 10000 log messages didn't include the one we sent!");
    }

    #[test]
    fn log_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        log_info!("log_test: {}", logged_value);

        expect_message_in_debuglog(format!("[component_manager] INFO: log_test: {}", logged_value));
    }

    #[test]
    fn log_fatal_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        let result = panic::catch_unwind(|| log_fatal!("fatal_test: {}", logged_value));
        assert!(result.is_err());

        expect_message_in_debuglog(format!(
            "[component_manager] FATAL: fatal_test: {}",
            logged_value
        ));
    }
}
