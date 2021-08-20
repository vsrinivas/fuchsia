// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use std::sync::Arc;

use crate::error;
use crate::logging::*;
use crate::types::*;

/// A type that can put a thread to sleep waiting for a condition.
#[derive(Debug)]
pub struct Waiter {
    /// The underlying Zircon port that the thread sleeps in.
    port: zx::Port,
}

impl Waiter {
    /// Create a new waiter object.
    pub fn new() -> Arc<Waiter> {
        Arc::new(Waiter { port: zx::Port::create().map_err(impossible_error).unwrap() })
    }

    /// Wait until the waiter is woken up.
    ///
    /// If the wait is interrupted (see interrupt), this function returns
    /// EINTR.
    pub fn wait(&self) -> Result<(), Errno> {
        self.wait_until(zx::Time::INFINITE)
    }

    /// Wait until the given deadline has passed or the waiter is woken up.
    ///
    /// If the wait is interrupted (seee interrupt), this function returns
    /// EINTR.
    pub fn wait_until(&self, deadline: zx::Time) -> Result<(), Errno> {
        match self.port.wait(deadline) {
            Ok(packet) => match packet.status() {
                zx::sys::ZX_OK => Ok(()),
                _ => error!(EINTR),
            },
            Err(zx::Status::TIMED_OUT) => Ok(()),
            Err(errno) => Err(impossible_error(errno)),
        }
    }

    /// Establish an asynchronous wait for the signals on the given handle.
    pub fn wait_async(
        &self,
        handle: &dyn zx::AsHandleRef,
        signals: zx::Signals,
    ) -> Result<(), zx::Status> {
        // TODO: Figure out what to do with the key. Presumably we'll need the
        // key to cancel the wait (e.g., for epoll).
        let key = 0;
        handle.wait_async_handle(&self.port, key, signals, zx::WaitAsyncOpts::empty())
    }

    /// Wake up the waiter.
    ///
    /// This function is called before the waiter goes to sleep, the waiter
    /// will wake up immediately upon attempting to go to sleep.
    pub fn wake(&self) {
        self.queue_user_packet(zx::sys::ZX_OK);
    }

    /// Interrupt the waiter.
    ///
    /// Used to break the waiter out of its sleep, for example to deliver an
    /// async signal. The wait operation will return EINTR, and unwind until
    /// the thread can process the async signal.
    #[allow(dead_code)]
    pub fn interrupt(&self) {
        self.queue_user_packet(zx::sys::ZX_ERR_CANCELED);
    }

    /// Queue a packet to the underlying Zircon port, which will cause the
    /// waiter to wake up.
    fn queue_user_packet(&self, status: i32) {
        let user_packet = zx::UserPacket::from_u8_array([0u8; 32]);
        let packet = zx::Packet::from_user_packet(0, status, user_packet);
        self.port.queue(&packet).map_err(impossible_error).unwrap();
    }
}
