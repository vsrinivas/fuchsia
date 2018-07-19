// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::mem;
use std::sync::Arc;

use executor::{EHandle, PacketReceiver, ReceiverRegistration};
use futures::task::{self, AtomicWaker};
use futures::{Async, Future, Poll};
use parking_lot::Mutex;
use zx::{self, AsHandleRef};

struct State {
    maybe_signals: u32,
    task: AtomicWaker,
}

struct OnSignalsReceiver {
    state: Arc<Mutex<State>>,
}

impl OnSignalsReceiver {
    fn get_signals(&self, cx: &mut task::Context) -> Async<zx::Signals> {
        let state = self.state.lock();
        if state.maybe_signals == 0 {
            state.task.register(cx.waker());
            Async::Pending
        } else {
            Async::Ready(zx::Signals::from_bits_truncate(state.maybe_signals))
        }
    }

    fn set_signals(&self, signals: zx::Signals) {
        let mut state = self.state.lock();
        state.maybe_signals = signals.bits();
        state.task.wake();
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
pub struct OnSignals(Result<ReceiverRegistration<OnSignalsReceiver>, zx::Status>);

impl OnSignals {
    /// Creates a new `OnSignals` object which will receive notifications when
    /// any signals in `signals` occur on `handle`.
    pub fn new<T>(handle: &T, signals: zx::Signals) -> Self
    where
        T: AsHandleRef,
    {
        let ehandle = EHandle::local();
        let receiver = ehandle.register_receiver(Arc::new(OnSignalsReceiver {
            state: Arc::new(Mutex::new(State {
                maybe_signals: 0,
                task: AtomicWaker::new(),
            })),
        }));

        let res = handle.wait_async_handle(
            receiver.port(),
            receiver.key(),
            signals,
            zx::WaitAsyncOpts::Once,
        );

        OnSignals(res.map(|()| receiver).map_err(Into::into))
    }
}

impl Future for OnSignals {
    type Item = zx::Signals;
    type Error = zx::Status;
    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        self.0
            .as_mut()
            .map(|receiver| receiver.receiver().get_signals(cx))
            .map_err(|e| mem::replace(e, zx::Status::OK))
    }
}

impl fmt::Debug for OnSignals {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "OnSignals")
    }
}
