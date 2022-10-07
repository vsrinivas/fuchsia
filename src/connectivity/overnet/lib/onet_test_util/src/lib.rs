// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::prelude::*;
use overnet_core::SecurityContext;
use rand::Rng;
use std::collections::VecDeque;
use std::pin::Pin;
use std::task::{Context, Poll, Waker};

#[cfg(not(target_os = "fuchsia"))]
pub fn test_security_context() -> Box<dyn SecurityContext> {
    return Box::new(
        overnet_core::MemoryBuffers {
            node_cert: include_bytes!(
                "../../../../../../third_party/rust_crates/mirrors/quiche/quiche/examples/cert.crt"
            ),
            node_private_key: include_bytes!(
                "../../../../../../third_party/rust_crates/mirrors/quiche/quiche/examples/cert.key"
            ),
            root_cert: include_bytes!(
                "../../../../../../third_party/rust_crates/mirrors/quiche/quiche/examples/rootca.crt"
            ),
        }
        .into_security_context(&std::env::temp_dir())
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

pub struct LosslessPipe {
    queue: VecDeque<u8>,
    read_waker: Option<Waker>,
}

impl LosslessPipe {
    pub fn new() -> Self {
        LosslessPipe { queue: VecDeque::new(), read_waker: None }
    }
}

impl AsyncRead for LosslessPipe {
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
                *b = x;
            } else {
                assert_ne!(i, 0);
                return Poll::Ready(Ok(i));
            }
        }
        Poll::Ready(Ok(bytes.len()))
    }
}

impl AsyncWrite for LosslessPipe {
    fn poll_write(
        mut self: Pin<&mut Self>,
        _ctx: &mut Context<'_>,
        bytes: &[u8],
    ) -> Poll<Result<usize, std::io::Error>> {
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
        tracing::trace!("LOSSY_WRITE: {:?}", std::str::from_utf8(bytes).unwrap());
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
