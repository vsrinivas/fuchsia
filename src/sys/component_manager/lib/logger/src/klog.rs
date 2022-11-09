// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon::{self as zx, AsHandleRef, ObjectType},
    std::fmt::{Debug, Write},
    tracing::{field::Field, Event, Level, Subscriber},
    tracing_log::LogTracer,
    tracing_subscriber::{field::Visit, layer::Context, prelude::*, Layer, Registry},
};

/// KernelLogger is a subscriber implementation for the tracing crate.
pub struct KernelLogger {
    debuglog: zx::DebugLog,
}

impl KernelLogger {
    /// Make a new `KernelLogger` by cloning our stdout and extracting the debuglog handle from it.
    fn new() -> KernelLogger {
        let debuglog = fdio::clone_fd(&std::io::stdout()).expect("get handle from stdout");
        assert_eq!(debuglog.basic_info().unwrap().object_type, ObjectType::DEBUGLOG);
        KernelLogger { debuglog: debuglog.into() }
    }

    /// Initialize the global subscriber to use KernelLogger and installs a forwarder for
    /// messages from the `log` crate.
    ///
    /// Registers a panic hook that prints the panic payload to the logger before running the
    /// default panic hook.
    pub fn init() {
        let subscriber = Registry::default().with(Self::new());
        tracing::subscriber::set_global_default(subscriber)
            .expect("init() should only be called once");
        LogTracer::init().expect("must be able to install log forwarder");

        let previous_hook = std::panic::take_hook();
        std::panic::set_hook(Box::new(move |info| {
            tracing::error!("PANIC {}", info);
            previous_hook(info);
        }));
    }
}

impl<S: Subscriber> Layer<S> for KernelLogger {
    fn on_event(&self, event: &Event<'_>, _cx: Context<'_, S>) {
        // tracing levels run the opposite direction of fuchsia severity
        let level = event.metadata().level();
        if *level <= Level::INFO {
            let mut visitor = StringVisitor("".to_string());
            event.record(&mut visitor);
            let mut msg = visitor.0;

            // msg always has a leading space
            msg = msg.trim_start().to_string();
            let msg_prefix = format!("[component_manager] {}: ", level);

            while msg.len() > 0 {
                // TODO(fxbug.dev/32998): zx_debuglog_write also accepts options and the possible options include
                // log levels, but they seem to be mostly unused and not displayed today, so we don't pass
                // along log level yet.
                let msg_to_write = format!("{}{}", msg_prefix, msg);
                if let Err(s) = self.debuglog.write(msg_to_write.as_bytes()) {
                    eprintln!("failed to write log ({}): {}", s, msg);
                }
                let num_to_drain =
                    std::cmp::min(msg.len(), zx::sys::ZX_LOG_RECORD_DATA_MAX - msg_prefix.len());
                msg.drain(..num_to_drain);
            }
        }
    }
}

struct StringVisitor(String);

impl StringVisitor {
    fn record_field(&mut self, field: &Field, value: std::fmt::Arguments<'_>) {
        match field.name() {
            "log.target" | "log.module_path" | "log.file" | "log.line" => {
                // don't write these fields to the klog
                return;
            }
            "message" => self.0.push(' '),
            name => {
                write!(self.0, " {name}=").expect("writing into strings does not fail");
            }
        }
        write!(self.0, "{}", value).expect("writing into strings does not fail");
    }
}

impl Visit for StringVisitor {
    fn record_debug(&mut self, field: &Field, value: &dyn Debug) {
        self.record_field(field, format_args!("{value:?}"));
    }

    fn record_str(&mut self, field: &Field, value: &str) {
        self.record_field(field, format_args!("{value}"));
    }

    fn record_i64(&mut self, field: &Field, value: i64) {
        self.record_field(field, format_args!("{value}"));
    }

    fn record_u64(&mut self, field: &Field, value: u64) {
        self.record_field(field, format_args!("{value}"));
    }

    fn record_bool(&mut self, field: &Field, value: bool) {
        self.record_field(field, format_args!("{value}"));
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Context,
        fidl_fuchsia_boot as fboot,
        fuchsia_component::client::connect_channel_to_protocol,
        fuchsia_zircon::{AsHandleRef, HandleBased},
        rand::Rng,
        std::panic,
        tracing::{error, info, warn},
    };

    fn get_root_resource() -> zx::Resource {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        connect_channel_to_protocol::<fboot::RootResourceMarker>(server_end).unwrap();
        let service = fboot::RootResourceSynchronousProxy::new(client_end);
        let resource = service.get(zx::Time::INFINITE).expect("couldn't get root resource");
        resource
    }

    // expect_message_in_debuglog will read the last 10000 messages in zircon's debuglog, looking
    // for a message that equals `sent_msg`. If found, the function returns. If the first 10,000
    // messages doesn't contain `sent_msg`, it will panic.
    fn expect_message_in_debuglog(sent_msg: String) {
        let resource = get_root_resource();
        let debuglog = zx::DebugLog::create(&resource, zx::DebugLogOpts::READABLE).unwrap();
        for _ in 0..10000 {
            match debuglog.read() {
                Ok(record) => {
                    let log = &record.data[..record.datalen as usize];
                    if String::from_utf8_lossy(log).starts_with(&sent_msg) {
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
        const STDOUT_FD: i32 = 1;

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
                "[component_manager] PANIC: panicked at 'panic_test {}'",
                logged_value
            ));
        }));

        init();
        panic!("panic_test {}", logged_value);
    }
}
