// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(target_os = "fuchsia")]

//! An implementation of a server for a fidl interface.

use {
    fuchsia_async as fasync,
    futures::task::{AtomicWaker, Context},
    std::sync::atomic::{self, AtomicBool},
};

/// A type used from the innards of server implementations
#[derive(Debug)]
pub struct ServeInner {
    waker: AtomicWaker,
    shutdown: AtomicBool,
    channel: fasync::Channel,
}

impl ServeInner {
    /// Create a new set of server innards.
    pub fn new(channel: fasync::Channel) -> Self {
        let waker = AtomicWaker::new();
        let shutdown = AtomicBool::new(false);
        ServeInner { waker, shutdown, channel }
    }

    /// Get a reference to the inner channel.
    pub fn channel(&self) -> &fasync::Channel {
        &self.channel
    }

    /// Set the server to shutdown.
    pub fn shutdown(&self) {
        self.shutdown.store(true, atomic::Ordering::Relaxed);
        self.waker.wake();
    }

    /// Check if the server has been set to shutdown.
    pub fn poll_shutdown(&self, cx: &mut Context<'_>) -> bool {
        if self.shutdown.load(atomic::Ordering::Relaxed) {
            return true;
        }
        self.waker.register(cx.waker());
        self.shutdown.load(atomic::Ordering::Relaxed)
    }
}
