// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Support for creating futures that represent timeouts.
//!
//! This module contains the `Timeout` type which is a future that will resolve
//! at a particular point in the future.

use std::marker::PhantomData;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

use futures::{Future, Stream, Poll, Async};
use futures::task::AtomicTask;

use executor::{EHandle, ReceiverRegistration, PacketReceiver};

use zx;
use zx::prelude::*;

/// The packet reciever for timeouts.
#[derive(Debug)]
struct TimeoutReceiver {
    task: AtomicTask,
    did_fire: AtomicBool,
}

impl PacketReceiver for TimeoutReceiver {
    fn receive_packet(&self, packet: zx::Packet) {
        if let zx::PacketContents::SignalRep(signals) = packet.contents() {
            if signals.observed().contains(zx::Signals::TIMER_SIGNALED) {
                self.did_fire.store(true, Ordering::SeqCst);
                self.task.notify();
            }
        }
    }
}

/// An asynchronous timeout.
#[derive(Debug)]
#[must_use = "futures do nothing unless polled"]
pub struct Timeout<E> {
    timer: zx::Timer,
    timeout_receiver: ReceiverRegistration<TimeoutReceiver>,
    _marker: PhantomData<E>,
}

impl<E> Timeout<E> {
    /// Create a new timeout scheduled to fire at `time`.
    pub fn new(time: zx::Time, ehandle: &EHandle) -> Result<Self, zx::Status> {
        let timeout_receiver = ehandle.register_receiver(
            Arc::new(TimeoutReceiver {
                task: AtomicTask::new(),
                did_fire: AtomicBool::from(false),
            })
        );

        let timer = zx::Timer::create(zx::ClockId::Monotonic)?;

        timer.wait_async_handle(
            ehandle.port(),
            timeout_receiver.key(),
            zx::Signals::TIMER_SIGNALED,
            zx::WaitAsyncOpts::Repeating,
        )?;

        timer.set(time, 0.nanos())?;

        Ok(Timeout {
            timer,
            timeout_receiver,
            _marker: PhantomData,
        })
    }

    /// Reset the `Timeout` to a fire at a new time.
    pub fn reset(&mut self, time: zx::Time) -> Result<(), zx::Status> {
        self.timeout_receiver.receiver().did_fire.store(false, Ordering::SeqCst);
        self.timer.set(time, 0.nanos())
    }

    fn did_fire(&self) -> bool {
        self.timeout_receiver.receiver().did_fire.load(Ordering::SeqCst)
    }

    fn register_task(&self) {
        self.timeout_receiver.receiver().task.register();
    }
}

impl<E> Future for Timeout<E> {
    type Item = ();
    type Error = E;
    fn poll(&mut self) -> Poll<(), E> {
        if self.did_fire() {
            Ok(Async::Ready(()))
        } else {
            self.register_task();
            Ok(Async::NotReady)
        }
    }
}

/// An asynchronous interval timer.
/// This is a stream of events resolving at a rate of once-per interval.
#[derive(Debug)]
#[must_use = "streams do nothing unless polled"]
pub struct Interval {
    timeout: Timeout<zx::Status>,
    next: zx::Time,
    duration: zx::Duration,
}

impl Interval {
    /// Create a new `Interval` which yields every `duration`.
    pub fn new(duration: zx::Duration, ehandle: &EHandle) -> Result<Self, zx::Status> {
        let next = duration.after_now();
        Ok(Interval {
            timeout: Timeout::new(next, ehandle)?,
            next,
            duration,
        })
    }

    /// Reset the `Interval` to a fire at a new rate.
    pub fn reset(&mut self, duration: zx::Duration) -> Result<(), zx::Status> {
        self.duration = duration;
        let new_next = duration.after_now();
        self.next = new_next;
        self.timeout.reset(new_next)
    }
}

impl Stream for Interval {
    type Item = ();
    type Error = zx::Status;
    fn poll(&mut self) -> Poll<Option<()>, Self::Error> {
        match self.timeout.poll() {
            Ok(Async::Ready(())) => {
                self.timeout.register_task();
                self.next += self.duration;
                self.timeout.reset(self.next)?;
                Ok(Async::Ready(Some(())))
            }
            Ok(Async::NotReady) => {
                self.timeout.register_task();
                Ok(Async::NotReady)
            }
            Err(e) => Err(e),
        }
    }
}
