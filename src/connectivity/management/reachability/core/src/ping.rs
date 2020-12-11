// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Context as _;
use async_trait::async_trait;
use futures::{Future, StreamExt as _, TryStreamExt as _};
use std::ffi::CString;

/// trait that can ping.
#[async_trait]
pub trait Pinger {
    /// returns true if url is reachable, false otherwise.
    async fn ping(&mut self, url: &str) -> bool;
}

#[link(name = "ext_ping", kind = "static")]
extern "C" {
    fn c_ping(url: *const libc::c_char) -> libc::ssize_t;
}

/// A ping implementation which requests a backend to send the echo requests via channels.
// TODO(fxbug.dev/65581) Replace this with an implementation in Rust.
pub struct IcmpPinger {
    request_tx: futures::channel::mpsc::UnboundedSender<String>,
    response_rx: futures::channel::mpsc::UnboundedReceiver<bool>,
}

impl IcmpPinger {
    pub fn new(
        request_tx: futures::channel::mpsc::UnboundedSender<String>,
        response_rx: futures::channel::mpsc::UnboundedReceiver<bool>,
    ) -> Self {
        Self { request_tx, response_rx }
    }
}

#[async_trait]
impl Pinger for IcmpPinger {
    // returns true if there has been a response, false otherwise.
    async fn ping(&mut self, url: &str) -> bool {
        match self.request_tx.unbounded_send(url.to_string()) {
            Ok(()) => {}
            Err(e) => panic!("failed to send request to ping URL={}: {}", url, e),
        }
        if let Some(rtn) = self.response_rx.next().await {
            rtn
        } else {
            panic!("ping response channel closed unexpectedly");
        }
    }
}

/// Creates a future which reads URLs from `request_rx`, and sends whether the URL is reachable via
/// ICMP echo requests to `response_tx`.
///
/// TODO(fxbug.dev/65581) The current implementation in C++ is blocking, and is required to be on a
/// separate thread as to not block other logic from running. This should be changed to an
/// implementation in Rust.
pub fn ping_fut(
    request_rx: futures::channel::mpsc::UnboundedReceiver<String>,
    response_tx: futures::channel::mpsc::UnboundedSender<bool>,
) -> impl Future<Output = Result<(), anyhow::Error>> {
    request_rx
        .map(move |url| {
            let ret;
            // unsafe needed as we are calling C code.
            let c_str = CString::new(url).unwrap();
            unsafe {
                ret = c_ping(c_str.as_ptr());
            }

            response_tx.unbounded_send(ret == 0).context("failed to send ping response")
        })
        .try_collect()
}
