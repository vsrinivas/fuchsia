// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
    std::fmt,
    std::panic,
};

const MAX_LOG_LEVEL: log::Level = log::Level::Info;

/// KernelLogger is a logger implementation for the log crate. It attempts to use the kernel
/// debuglog facility and automatically falls back to stderr if that fails.
pub struct KernelLogger {
    debuglog: zx::sys::zx_handle_t,
}

lazy_static! {
    static ref LOGGER: KernelLogger = KernelLogger::new();
}

impl KernelLogger {
    fn new() -> KernelLogger {
        // TODO: Create zx::DebugLog wrappers for zx_debuglog_* syscalls to avoid direct unsafe
        // usage.
        let mut log_handle = zx::sys::ZX_HANDLE_INVALID;
        let status = unsafe {
            zx::sys::zx_debuglog_create(
                zx::sys::ZX_HANDLE_INVALID,
                0,
                &mut log_handle as *mut zx::sys::zx_handle_t,
            )
        };
        zx::Status::ok(status).context("Failed to create debuglog object").unwrap();
        KernelLogger{debuglog: log_handle}
    }

    /// Initialize the global logger to use KernelLogger.
    ///
    /// Also registers a panic hook that prints the panic payload to the logger before running the
    /// default panic hook.
    ///
    /// Returns an error if this or something else already called [log::set_logger()].
    pub fn init() -> Result<(), Error> {
        log::set_logger(&*LOGGER).context("Failed to set KernelLogger as global logger")?;
        log::set_max_level(MAX_LOG_LEVEL.to_level_filter());
        Self::install_panic_hook();
        Ok(())
    }

    /// Register a panic hook that prints the panic payload to the logger before running the
    /// default panic hook.
    pub fn install_panic_hook() {
        let default_hook = panic::take_hook();
        panic::set_hook(Box::new(move |panic_info| {
            // Handle common cases of &'static str or String payload.
            let msg = match panic_info.payload().downcast_ref::<&'static str>() {
                    Some(s) => *s,
                    None => match panic_info.payload().downcast_ref::<String>() {
                        Some(s) => &s[..],
                        None => "<Unknown panic payload type>",
                    }
                };
            LOGGER.log_helper("PANIC", &format_args!("{}", msg));

            default_hook(panic_info);
        }));
    }

    fn log_helper(&self, level: &str, args: &fmt::Arguments) {
        let msg = format!("[component_manager] {}: {}", level, args);

        // TODO: Create zx::DebugLog wrappers for zx_debuglog_* syscalls to avoid direct unsafe
        // usage.
        // TODO: zx_debuglog_write also accepts options and the possible options include log
        // levels, but they seem to be mostly unused and not displayed today, so we don't pass
        // along log level yet.
        let status = unsafe {
            zx::sys::zx_debuglog_write(
                self.debuglog,
                0,
                msg.as_ptr(),
                msg.as_bytes().len(),
            )
        };
        if let Err(s) = zx::Status::ok(status) {
            eprintln!("failed to write log ({}): {}", s, msg);
        }
    }
}

impl log::Log for KernelLogger {
    fn enabled(&self, metadata: &log::Metadata) -> bool {
        metadata.level() <= MAX_LOG_LEVEL
    }

    fn log(&self, record: &log::Record) {
        if self.enabled(record.metadata()) {
            self.log_helper(&record.level().to_string(), record.args());
        }
    }

    fn flush(&self) {}
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        byteorder::{ByteOrder, LittleEndian},
        log::*,
        rand::Rng,
        std::{panic, sync::Once, thread, time},
    };

    const ZX_LOG_FLAG_READABLE: u32 = 0x40000000;

    // KernelLogger::init/log::set_logger will fail if called more than once and there's no way to
    // reset.
    fn init_logger_once() {
        static INIT: Once = Once::new();
        INIT.call_once(|| {
            KernelLogger::init().unwrap();
        });
    }

    // expect_message_in_debuglog will read the first 10000 messages in zircon's debuglog, looking
    // for a message that equals `sent_msg`. If found, the function returns. If the first 10,000
    // messages doesn't contain `sent_msg`, it will panic.
    fn expect_message_in_debuglog(sent_msg: String) {
        // TODO: Create zx::DebugLog wrappers for zx_debuglog_* syscalls to avoid direct unsafe
        // usage.
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
    fn log_info_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        init_logger_once();
        info!("log_test {}", logged_value);

        expect_message_in_debuglog(format!("[component_manager] INFO: log_test {}", logged_value));
    }

    #[test]
    fn log_warn_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        init_logger_once();
        warn!("log_test {}", logged_value);

        expect_message_in_debuglog(format!("[component_manager] WARN: log_test {}", logged_value));
    }

    #[test]
    fn log_error_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        init_logger_once();
        error!("log_test {}", logged_value);

        expect_message_in_debuglog(format!("[component_manager] ERROR: log_test {}", logged_value));
    }

    #[test]
    fn log_panic_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        init_logger_once();
        let result = panic::catch_unwind(|| panic!("panic_test {}", logged_value));
        assert!(result.is_err());

        expect_message_in_debuglog(format!(
            "[component_manager] PANIC: panic_test {}",
            logged_value
        ));
    }
}
