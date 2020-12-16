// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use futures::prelude::*;
use overnet_core::SecurityContext;
use rand::Rng;
use std::collections::VecDeque;
use std::pin::Pin;
use std::sync::Once;
use std::task::{Context, Poll, Waker};

const LOG_LEVEL: log::Level = log::Level::Info;
const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

struct Logger;

fn short_log_level(level: &log::Level) -> &'static str {
    match *level {
        log::Level::Error => "E",
        log::Level::Warn => "W",
        log::Level::Info => "I",
        log::Level::Debug => "D",
        log::Level::Trace => "T",
    }
}

impl log::Log for Logger {
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() <= LOG_LEVEL
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            let msg = format!(
                "{:?} {:?} {} {} [{}]: {}",
                std::time::Instant::now(),
                std::thread::current().id(),
                record.target(),
                record
                    .file()
                    .map(|file| {
                        if let Some(line) = record.line() {
                            format!("{}:{}: ", file, line)
                        } else {
                            format!("{}: ", file)
                        }
                    })
                    .unwrap_or(String::new()),
                short_log_level(&record.level()),
                record.args()
            );
            let _ = std::panic::catch_unwind(|| {
                println!("{}", msg);
            });
        }
    }

    fn flush(&self) {}
}

static LOGGER: Logger = Logger;
static START: Once = Once::new();

pub fn init() {
    START.call_once(|| {
        log::set_logger(&LOGGER).unwrap();
        log::set_max_level(MAX_LOG_LEVEL);
    })
}

#[cfg(not(target_os = "fuchsia"))]
pub fn test_security_context() -> Box<dyn SecurityContext> {
    return Box::new(
        overnet_core::MemoryBuffers {
            node_cert: include_bytes!(
                "../../../../../../third_party/rust-mirrors/quiche/examples/cert.crt"
            ),
            node_private_key: include_bytes!(
                "../../../../../../third_party/rust-mirrors/quiche/examples/cert.key"
            ),
            root_cert: include_bytes!(
                "../../../../../../third_party/rust-mirrors/quiche/examples/rootca.crt"
            ),
        }
        .into_security_context()
        .unwrap(),
    );
}

#[cfg(target_os = "fuchsia")]
pub fn test_security_context() -> Box<dyn SecurityContext> {
    Box::new(overnet_core::SimpleSecurityContext {
        node_cert: "/pkg/data/cert.crt",
        node_private_key: "/pkg/data/cert.key",
        root_cert: "/pkg/data/rootca.crt",
    })
}

pub struct DodgyPipe {
    failures_per_64kib: u16,
    queue: VecDeque<u8>,
    read_waker: Option<Waker>,
}

impl DodgyPipe {
    pub fn new(failures_per_64kib: u16) -> DodgyPipe {
        DodgyPipe { failures_per_64kib, queue: VecDeque::new(), read_waker: None }
    }
}

impl AsyncRead for DodgyPipe {
    fn poll_read(
        mut self: Pin<&mut Self>,
        ctx: &mut Context<'_>,
        bytes: &mut [u8],
    ) -> Poll<Result<usize, std::io::Error>> {
        if self.queue.is_empty() {
            self.read_waker = Some(ctx.waker().clone());
            return Poll::Pending;
        }
        for (i, b) in bytes.iter_mut().enumerate() {
            if let Some(x) = self.queue.pop_front() {
                if self.failures_per_64kib > rand::thread_rng().gen() {
                    *b = x ^ 0xff;
                } else {
                    *b = x;
                }
            } else {
                assert_ne!(i, 0);
                return Poll::Ready(Ok(i));
            }
        }
        Poll::Ready(Ok(bytes.len()))
    }
}

impl AsyncWrite for DodgyPipe {
    fn poll_write(
        mut self: Pin<&mut Self>,
        _ctx: &mut Context<'_>,
        bytes: &[u8],
    ) -> Poll<Result<usize, std::io::Error>> {
        log::trace!("DODGY_WRITE:{:?}", std::str::from_utf8(bytes).unwrap());
        self.queue.extend(bytes.iter());
        self.read_waker.take().map(|w| w.wake());
        Poll::Ready(Ok(bytes.len()))
    }

    fn poll_flush(
        self: Pin<&mut Self>,
        _ctx: &mut Context<'_>,
    ) -> Poll<Result<(), std::io::Error>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(
        self: Pin<&mut Self>,
        _ctx: &mut Context<'_>,
    ) -> Poll<Result<(), std::io::Error>> {
        Poll::Ready(Ok(()))
    }
}
