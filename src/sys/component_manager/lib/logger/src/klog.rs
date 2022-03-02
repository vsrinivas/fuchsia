// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fdio::fdio_sys,
    fuchsia_zircon::{self as zx, AsHandleRef, ObjectType},
    lazy_static::lazy_static,
    std::fmt,
    std::{panic, sync::Once},
};

const STDOUT_FD: i32 = 1;
const MAX_LOG_LEVEL: log::Level = log::Level::Info;

lazy_static! {
    pub static ref LOGGER: KernelLogger = KernelLogger::new();
}

/// KernelLogger is a logger implementation for the log crate. It attempts to use the kernel
/// debuglog facility and automatically falls back to stderr if that fails.
pub struct KernelLogger {
    debuglog: zx::DebugLog,
}

impl KernelLogger {
    fn new() -> KernelLogger {
        let debuglog = unsafe {
            let mut raw_debuglog = zx::sys::ZX_HANDLE_INVALID;
            let status = fdio_sys::fdio_fd_clone(STDOUT_FD, &mut raw_debuglog);
            if let Err(s) = zx::Status::ok(status) {
                // Panic as this failure means that there's no logger initialized.
                panic!("Unable to get debuglog handle from stdout fd: {}", s);
            }
            fuchsia_zircon::Handle::from_raw(raw_debuglog)
        };

        assert_eq!(debuglog.basic_info().unwrap().object_type, ObjectType::LOG);
        KernelLogger { debuglog: debuglog.into() }
    }

    /// Initialize the global logger to use KernelLogger.
    ///
    /// Also registers a panic hook that prints the panic payload to the logger before running the
    /// default panic hook.
    ///
    /// This function is idempotent, and will not re-initialize the global logger on subsequent
    /// calls.
    pub fn init() {
        static INIT: Once = Once::new();
        INIT.call_once(|| {
            log::set_logger(&*LOGGER).expect("Failed to set KernelLogger as global logger");
            log::set_max_level(MAX_LOG_LEVEL.to_level_filter());
            Self::install_panic_hook();
        });
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
                },
            };
            LOGGER.log_helper("PANIC", &format_args!("{}", msg));

            default_hook(panic_info);
        }));
    }

    fn log_helper(&self, level: &str, args: &fmt::Arguments<'_>) {
        let mut msg = format!("[component_manager] {}: {}", level, args);

        while msg.len() > 0 {
            // TODO(fxbug.dev/32998): zx_debuglog_write also accepts options and the possible options include
            // log levels, but they seem to be mostly unused and not displayed today, so we don't pass
            // along log level yet.
            if let Err(s) = self.debuglog.write(msg.as_bytes()) {
                eprintln!("failed to write log ({}): {}", s, msg);
            }
            let num_to_drain = std::cmp::min(msg.len(), zx::sys::ZX_LOG_RECORD_DATA_MAX);
            msg.drain(..num_to_drain);
        }
    }
}

impl log::Log for KernelLogger {
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() <= MAX_LOG_LEVEL
    }

    fn log(&self, record: &log::Record<'_>) {
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
        anyhow::Context,
        fuchsia_zircon::{AsHandleRef, HandleBased},
        log::*,
        rand::Rng,
        std::panic,
    };

    // expect_message_in_debuglog will read the last 10000 messages in zircon's debuglog, looking
    // for a message that equals `sent_msg`. If found, the function returns. If the first 10,000
    // messages doesn't contain `sent_msg`, it will panic.
    fn expect_message_in_debuglog(sent_msg: String) {
        let resource = zx::Resource::from(zx::Handle::invalid());
        let debuglog = zx::DebugLog::create(&resource, zx::DebugLogOpts::READABLE).unwrap();
        for _ in 0..10000 {
            match debuglog.read() {
                Ok(record) => {
                    let log = &record.data[..record.datalen as usize];
                    if log == sent_msg.as_bytes() {
                        // We found our log!
                        return;
                    }
                }
                Err(status) if status == zx::Status::SHOULD_WAIT => {
                    debuglog
                        .wait_handle(zx::Signals::LOG_READABLE, zx::Time::INFINITE)
                        .expect("Failed to wait for log readable");
                    continue;
                }
                Err(status) => {
                    panic!("Unexpected error from zx_debuglog_read: {}", status);
                }
            }
        }
        panic!("first 10000 log messages didn't include the one we sent!");
    }

    // Userboot passes component manager a debuglog handle tied to fd 0/1/2, which component
    // manager now uses to retrieve the debuglog handle. To simulate that, we need to bind
    // a handle before calling KernelLogger::init().
    fn init() {
        let resource = zx::Resource::from(zx::Handle::invalid());
        let debuglog = zx::DebugLog::create(&resource, zx::DebugLogOpts::empty())
            .context("Failed to create debuglog object")
            .unwrap();
        fdio::bind_to_fd(debuglog.into_handle(), STDOUT_FD).unwrap();

        KernelLogger::init();
    }

    #[test]
    fn log_info_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        init();
        info!("log_test {}", logged_value);

        expect_message_in_debuglog(format!("[component_manager] INFO: log_test {}", logged_value));
    }

    #[test]
    fn log_warn_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        init();
        warn!("log_test {}", logged_value);

        expect_message_in_debuglog(format!("[component_manager] WARN: log_test {}", logged_value));
    }

    #[test]
    fn log_error_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        init();
        error!("log_test {}", logged_value);

        expect_message_in_debuglog(format!("[component_manager] ERROR: log_test {}", logged_value));
    }

    #[test]
    #[should_panic(expected = "panic_test")]
    fn log_panic_test() {
        let mut rng = rand::thread_rng();
        let logged_value: u64 = rng.gen();

        let old_hook = panic::take_hook();
        panic::set_hook(Box::new(move |info| {
            // This will panic again if the message is not found,
            // and the message will not include "panic_test".
            old_hook(info);
            expect_message_in_debuglog(format!(
                "[component_manager] PANIC: panic_test {}",
                logged_value
            ));
        }));

        init();
        panic!("panic_test {}", logged_value);
    }
}
