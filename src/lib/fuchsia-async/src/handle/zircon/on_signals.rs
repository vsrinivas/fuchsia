// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::future::Future;
use std::mem;
use std::pin::Pin;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::task::Poll;

use crate::runtime::{EHandle, PacketReceiver, ReceiverRegistration};
use fuchsia_zircon::{self as zx, AsHandleRef};
use futures::task::{AtomicWaker, Context};

struct OnSignalsReceiver {
    maybe_signals: AtomicUsize,
    task: AtomicWaker,
}

impl OnSignalsReceiver {
    fn get_signals(&self, cx: &mut Context<'_>) -> Poll<zx::Signals> {
        let mut signals = self.maybe_signals.load(Ordering::Relaxed);
        if signals == 0 {
            // No signals were received-- register to receive a wakeup when they arrive.
            self.task.register(cx.waker());
            // Check again for signals after registering for a wakeup in case signals
            // arrived between registering and the initial load of signals
            signals = self.maybe_signals.load(Ordering::SeqCst);
        }
        if signals == 0 {
            Poll::Pending
        } else {
            Poll::Ready(zx::Signals::from_bits_truncate(signals as u32))
        }
    }

    fn set_signals(&self, signals: zx::Signals) {
        self.maybe_signals.store(signals.bits() as usize, Ordering::SeqCst);
        self.task.wake();
    }
}

impl PacketReceiver for OnSignalsReceiver {
    fn receive_packet(&self, packet: zx::Packet) {
        let observed = if let zx::PacketContents::SignalOne(p) = packet.contents() {
            p.observed()
        } else {
            return;
        };

        self.set_signals(observed);
    }
}

/// A future that completes when some set of signals become available on a Handle.
#[must_use = "futures do nothing unless polled"]
pub struct OnSignals<'a> {
    state: Result<ReceiverRegistration<OnSignalsReceiver>, zx::Status>,
    handle: Option<(zx::HandleRef<'a>, EHandle)>,
}

impl<'a> OnSignals<'a> {
    /// Creates a new `OnSignals` object which will receive notifications when
    /// any signals in `signals` occur on `handle`.
    pub fn new<T: AsHandleRef>(handle: &'a T, signals: zx::Signals) -> Self {
        Self::from_ref(handle.as_handle_ref(), signals)
    }

    /// Creates a new `OnSignals` using a HandleRef instead of an AsHandleRef.
    ///
    /// Passing a HandleRef to OnSignals::new is likely to lead to borrow check errors, since the
    /// resulting OnSignals is tied to the lifetime of the HandleRef itself and not the handle it
    /// refers to. Use this instead when you need to pass a HandleRef.
    pub fn from_ref(handle: zx::HandleRef<'a>, signals: zx::Signals) -> Self {
        let ehandle = EHandle::local();
        let receiver = ehandle.register_receiver(Arc::new(OnSignalsReceiver {
            maybe_signals: AtomicUsize::new(0),
            task: AtomicWaker::new(),
        }));

        let res = handle.wait_async_handle(
            receiver.port(),
            receiver.key(),
            signals,
            zx::WaitAsyncOpts::empty(),
        );

        OnSignals {
            state: res.map(|()| receiver).map_err(Into::into),
            handle: Some((handle, ehandle)),
        }
    }

    /// This function allows the `OnSignals` object to live for the `'static` lifetime, at the cost
    /// of disabling automatic cleanup of the port wait.
    ///
    /// WARNING: Do not use unless you can guarantee that either:
    /// - The future is not dropped before it completes, or
    /// - The handle is dropped without creating additional OnSignals futures for it.
    ///
    /// Creating an OnSignals calls zx_object_wait_async, which consumes a small amount of kernel
    /// resources. Dropping the OnSignals calls zx_port_cancel to clean up. But calling
    /// extend_lifetime disables this cleanup, since the zx_port_wait call requires a reference to
    /// the handle. The port registration can also be cleaned up by closing the handle or by
    /// waiting for the signal to be triggered. But if neither of these happens, the registration
    /// is leaked. This wastes kernel memory and the kernel will eventually kill your process to
    /// force a cleanup.
    ///
    /// Note that `OnSignals` will not fire if the handle that was used to create it is dropped or
    /// transferred to another process.
    // TODO(fxbug.dev/99577): Try to remove this footgun.
    pub fn extend_lifetime(mut self) -> OnSignals<'static> {
        OnSignals { state: std::mem::replace(&mut self.state, Err(zx::Status::OK)), handle: None }
    }
}

impl Unpin for OnSignals<'_> {}

impl Future for OnSignals<'_> {
    type Output = Result<zx::Signals, zx::Status>;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let reg = self.state.as_mut().map_err(|e| mem::replace(e, zx::Status::OK))?;
        reg.receiver().get_signals(cx).map(Ok)
    }
}

impl fmt::Debug for OnSignals<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "OnSignals")
    }
}

impl Drop for OnSignals<'_> {
    fn drop(&mut self) {
        if let (Ok(receiver), Some((handle, ehandle))) = (&self.state, &self.handle) {
            if receiver.receiver().maybe_signals.load(Ordering::SeqCst) == 0 {
                // Ignore the error from zx_port_cancel, because it might just be a race condition.
                // If the packet is handled between the above maybe_signals check and the port
                // cancel, it will fail with ZX_ERR_NOT_FOUND, and we can't do anything about it.
                let _ = ehandle.port().cancel(&*handle, receiver.key());
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::future::{pending, FutureExt};

    #[test]
    fn wait_for_event() -> Result<(), zx::Status> {
        let mut exec = crate::TestExecutor::new()?;
        let mut deliver_events =
            || assert!(exec.run_until_stalled(&mut pending::<()>()).is_pending());

        let event = zx::Event::create()?;
        let mut signals = OnSignals::new(&event, zx::Signals::EVENT_SIGNALED);
        let (waker, waker_count) = futures_test::task::new_count_waker();
        let cx = &mut std::task::Context::from_waker(&waker);

        // Check that `signals` is still pending before the event has been signaled
        assert_eq!(signals.poll_unpin(cx), Poll::Pending);
        deliver_events();
        assert_eq!(waker_count, 0);
        assert_eq!(signals.poll_unpin(cx), Poll::Pending);

        // signal the event and check that `signals` has been woken up and is
        // no longer pending
        event.signal_handle(zx::Signals::NONE, zx::Signals::EVENT_SIGNALED)?;
        deliver_events();
        assert_eq!(waker_count, 1);
        assert_eq!(signals.poll_unpin(cx), Poll::Ready(Ok(zx::Signals::EVENT_SIGNALED)));

        Ok(())
    }

    #[test]
    fn drop_before_event() -> Result<(), zx::Status> {
        let _exec = crate::TestExecutor::new()?;
        let ehandle = EHandle::local();

        let event = zx::Event::create()?;
        let signals = OnSignals::new(&event, zx::Signals::EVENT_SIGNALED);
        let key = signals.state.as_ref().unwrap().key();

        std::mem::drop(signals);
        assert!(ehandle.port().cancel(&event, key) == Err(zx::Status::NOT_FOUND));

        // try again but with extend_lifetime
        let signals = OnSignals::new(&event, zx::Signals::EVENT_SIGNALED).extend_lifetime();
        let key = signals.state.as_ref().unwrap().key();
        std::mem::drop(signals);
        assert!(ehandle.port().cancel(&event, key) == Ok(()));

        Ok(())
    }
}
