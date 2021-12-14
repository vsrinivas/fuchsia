// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::terminal::Terminal,
    anyhow::Error,
    fuchsia_async::{self as fasync, OnSignals},
    fuchsia_runtime,
    fuchsia_zircon::{self as zx, AsHandleRef},
    std::io::sink,
    term_model::{ansi::Processor, event::EventListener},
};

pub trait LogClient: 'static + Clone {
    type Listener;

    fn create_terminal(&self, id: u32, title: String) -> Result<Terminal<Self::Listener>, Error>;
    fn request_update(&self, id: u32);
}

pub struct Log;

impl Log {
    pub fn start<T: LogClient>(
        read_only_debuglog: zx::DebugLog,
        client: &T,
        id: u32,
    ) -> Result<(), Error>
    where
        <T as LogClient>::Listener: EventListener,
    {
        let client = client.clone();
        let terminal =
            client.create_terminal(id, "debuglog".to_string()).expect("failed to create terminal");
        let term = terminal.clone_term();

        // Get our process koid so we can filter out our own debug messages from the log.
        let proc_koid =
            fuchsia_runtime::process_self().get_koid().expect("failed to get koid for process");

        fasync::Task::local(async move {
            let mut sink = sink();
            let mut parser = Processor::new();

            loop {
                let on_signal = OnSignals::new(&read_only_debuglog, zx::Signals::LOG_READABLE);
                on_signal.await.expect("failed to wait for log readable");

                loop {
                    match read_only_debuglog.read() {
                        Ok(record) => {
                            // Don't print log messages from ourself.
                            if record.pid == proc_koid.raw_koid() {
                                continue;
                            }

                            let mut term = term.borrow_mut();

                            // Write prefix with time stamps and ids.
                            let prefix = format!(
                                "\u{001b}[32m{:05}.{:03}\u{001b}[39m] \u{001b}[31m{:05}.\u{001b}[36m{:05}\u{001b}[39m> ",
                                record.timestamp / 1_000_000_000,
                                (record.timestamp / 1_000_000) % 1_000,
                                record.pid,
                                record.tid,
                            );
                            for byte in prefix.as_bytes() {
                                parser.advance(&mut *term, *byte, &mut sink);
                            }

                            // Ignore any trailing newline character.
                            let mut datalen = record.datalen as usize;
                            if datalen > 0 && record.data[datalen - 1] == '\n' as u8 {
                                datalen -= 1;
                            }

                            // Write record data.
                            for byte in &record.data[0..datalen] {
                                parser.advance(&mut *term, *byte, &mut sink);
                            }

                            // Write carriage return and newline.
                            for byte in "\r\n".as_bytes() {
                                parser.advance(&mut *term, *byte, &mut sink);
                            }

                            // Request terminal update.
                            client.request_update(id);
                        }
                        Err(status) if status == zx::Status::SHOULD_WAIT => {
                            break;
                        }
                        Err(_) => {
                            let mut term = term.borrow_mut();
                            for byte in "\r\n<<LOG ERROR>>".as_bytes() {
                                parser.advance(&mut *term, *byte, &mut sink);
                            }

                            // Request terminal update.
                            client.request_update(id);
                            break;
                        }
                    }
                }
            }
        })
        .detach();

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::colors::ColorScheme,
        fuchsia_async as fasync,
        term_model::event::{Event, EventListener},
    };

    #[derive(Default)]
    struct TestListener;

    impl EventListener for TestListener {
        fn send_event(&self, _event: Event) {}
    }

    #[derive(Default, Clone)]
    struct TestLogClient;

    impl LogClient for TestLogClient {
        type Listener = TestListener;

        fn create_terminal(
            &self,
            _id: u32,
            title: String,
        ) -> Result<Terminal<Self::Listener>, Error> {
            Ok(Terminal::new(TestListener::default(), title, ColorScheme::default(), 1024, None))
        }
        fn request_update(&self, _id: u32) {}
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_start_log() -> Result<(), Error> {
        let resource = zx::Resource::from(zx::Handle::invalid());
        let debuglog = zx::DebugLog::create(&resource, zx::DebugLogOpts::empty()).unwrap();
        let client = TestLogClient::default();
        let _ = Log::start(debuglog, &client, 0)?;
        Ok(())
    }
}
