// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::marker::PhantomData;
use std::mem;
use std::pin::Pin;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::future::Future;
use std::task::Poll;

use crate::executor::{EHandle, PacketReceiver, ReceiverRegistration};
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
        self.maybe_signals
            .store(signals.bits() as usize, Ordering::SeqCst);
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
    // Prevent the `OnSignals` from living longer than the handle from which it was created.
    // This prevents bugs due to signals not being received for handles dropped or sent to
    // other processes.
    marker: PhantomData<&'a ()>,
}

impl<'a> OnSignals<'a> {
    /// Creates a new `OnSignals` object which will receive notifications when
    /// any signals in `signals` occur on `handle`.
    pub fn new<T>(handle: &'a T, signals: zx::Signals) -> Self
    where
        T: AsHandleRef,
    {
        let ehandle = EHandle::local();
        let receiver = ehandle.register_receiver(Arc::new(OnSignalsReceiver {
            maybe_signals: AtomicUsize::new(0),
            task: AtomicWaker::new(),
        }));

        let res = handle.wait_async_handle(
            receiver.port(),
            receiver.key(),
            signals,
            zx::WaitAsyncOpts::Once,
        );

        OnSignals {
            state: res.map(|()| receiver).map_err(Into::into),
            marker: PhantomData,
        }
    }

    /// This function allows the `OnSignals` object to live for the `'static` lifetime.
    ///
    /// It is functionally a no-op, but callers of this method should note that
    /// `OnSignals` will not fire if the handle that was used to create it is dropped or
    /// transferred to another process.
    pub fn extend_lifetime(self) -> OnSignals<'static> {
        OnSignals {
            state: self.state,
            marker: PhantomData,
        }
    }
}

impl Unpin for OnSignals<'_> {}

impl Future for OnSignals<'_> {
    type Output = Result<zx::Signals, zx::Status>;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let reg = self
            .state
            .as_mut()
            .map_err(|e| mem::replace(e, zx::Status::OK))?;
        reg.receiver().get_signals(cx).map(Ok)
    }
}

impl fmt::Debug for OnSignals<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "OnSignals")
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::future::{pending, FutureExt};

    #[test]
    fn wait_for_event() -> Result<(), zx::Status> {
        let mut exec = crate::Executor::new()?;
        let mut deliver_events = || assert!(exec.run_until_stalled(&mut pending::<()>()).is_pending());

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
}
