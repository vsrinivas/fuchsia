// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;
use futures::prelude::*;
use futures::task::{Context, Poll, Waker};
use parking_lot::Mutex;
use std::pin::Pin;

#[cfg(test)]
macro_rules! traceln (($($args:tt)*) => { eprintln!($($args)*); }; );

#[cfg(not(test))]
macro_rules! traceln (($($args:tt)*) => { }; );

/// FlowWindow is a object that is used for ensuring low-latency
/// flow control.
///
/// Instances of this struct act as a register that is never allowed
/// to be decremented to a value less than zero. If a decrement operation
/// would allow that to happen, then the method blocks asynchronously
/// until some other mechanism has incremented the value enough to allow
/// it to successfully complete.
///
/// Future versions of this type may further optimize behavior by ensuring
/// that the increment method only invokes the wakers of tasks that can
/// be successfully unblocked. For now, all pending tasks are awoken upon any
/// increment, and no effort is made to ensure fair queueing.
#[derive(Debug)]
pub struct FlowWindow {
    inner: Mutex<(u32, Vec<Waker>)>,
}

impl Default for FlowWindow {
    fn default() -> Self {
        FlowWindow { inner: Mutex::new((0, Vec::new())) }
    }
}

#[derive(Debug)]
pub struct FlowWindowDec<'a>(&'a FlowWindow, u32);

impl<'a> Future for FlowWindowDec<'a> {
    type Output = ();
    fn poll(mut self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        (&mut self.0).poll_dec(context, self.1)
    }
}

impl FlowWindow {
    pub fn poll_dec(&self, context: &mut Context<'_>, amount: u32) -> Poll<()> {
        let mut inner = self.inner.lock();
        traceln!(
            "FlowWindow::poll_dec: Caller needs {} slot(s), we have {} slot(s).",
            amount,
            inner.0
        );
        if inner.0 >= amount {
            traceln!("FlowWindow::poll_dec: Accepting, decrementing by {}", amount);
            inner.0 -= amount;
            Poll::Ready(())
        } else {
            traceln!("FlowWindow::poll_dec: Waiting for {} more slot(s)", amount - inner.0);
            inner.1.push(context.waker().clone());
            Poll::Pending
        }
    }

    #[allow(dead_code)]
    pub fn dec(&self, amount: u32) -> FlowWindowDec<'_> {
        FlowWindowDec(self, amount)
    }

    pub fn inc(&self, amount: u32) {
        let mut inner = self.inner.lock();
        traceln!(
            "FlowWindow::inc: Incrementing by {} slot(s), for a total of {} slot(s).",
            amount,
            amount + (*inner).0
        );
        (*inner).0 += amount;
        for waker in inner.1.drain(..) {
            traceln!("FlowWindow::inc: Calling waker");
            waker.wake();
        }
    }

    /// Resets the internal counter to zero.
    pub fn reset(&self) {
        let mut inner = self.inner.lock();
        traceln!("FlowWindow::reset");
        (*inner).0 = 0;
    }

    /// Method for getting the current value of the
    /// flow window. Intended to only be used for testing.
    #[cfg(test)]
    pub fn get(&self) -> u32 {
        self.inner.lock().0
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    #[fasync::run_until_stalled(test)]
    async fn test_flow_window() {
        let flow_window = FlowWindow::default();

        assert_eq!(flow_window.dec(0).now_or_never(), Some(()));

        assert_eq!(flow_window.dec(1).now_or_never(), None);

        flow_window.inc(2);

        assert_eq!(flow_window.dec(1).now_or_never(), Some(()));
        assert_eq!(flow_window.dec(1).now_or_never(), Some(()));
        assert_eq!(flow_window.dec(1).now_or_never(), None);

        flow_window.inc(1);

        assert_eq!(flow_window.dec(2).now_or_never(), None);

        flow_window.inc(2);

        assert_eq!(flow_window.dec(2).now_or_never(), Some(()));
        assert_eq!(flow_window.dec(2).now_or_never(), None);
        assert_eq!(flow_window.dec(1).now_or_never(), Some(()));

        futures::join!(flow_window.dec(1), async { flow_window.inc(1) });

        flow_window.inc(1);
        assert_eq!(flow_window.get(), 1);
        flow_window.reset();
        assert_eq!(flow_window.get(), 0);
        assert_eq!(flow_window.dec(1).now_or_never(), None);
    }
}
