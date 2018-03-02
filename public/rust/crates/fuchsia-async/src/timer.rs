// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Support for creating futures that represent timers.
//!
//! This module contains the `Timer` type which is a future that will resolve
//! at a particular point in the future.

use std::marker::PhantomData;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

use futures::{Future, Stream, Poll, Async, Never};
use futures::task::{self, AtomicWaker};

use executor::{EHandle, ReceiverRegistration, PacketReceiver};

use zx;
use zx::prelude::*;

/// A trait which allows futures to be easily wrapped in a timeout.
pub trait TimeoutExt: Future + Sized {
    /// Wraps the future in a timeout, calling `on_timeout` to produce a result
    /// when the timeout occurs.
    fn on_timeout<OT>(self, time: zx::Time, on_timeout: OT)
        -> Result<OnTimeout<Self, OT>, zx::Status>
        where OT: FnOnce() -> Result<Self::Item, Self::Error>
    {
        Ok(OnTimeout {
            timer: Timer::new(time)?,
            future: self,
            on_timeout: Some(on_timeout),
        })
    }
}

impl<F: Future + Sized> TimeoutExt for F {}

/// A wrapper for a future which will complete with a provided closure when a timeout occurs.
#[derive(Debug)]
#[must_use = "futures do nothing unless polled"]
pub struct OnTimeout<F: Future, OT> {
    timer: Timer<Never>,
    future: F,
    on_timeout: Option<OT>,
}

impl<F: Future, OT> Future for OnTimeout<F, OT>
    where OT: FnOnce() -> Result<F::Item, F::Error>
{
    type Item = F::Item;
    type Error = F::Error;
    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        if let Async::Ready(item) = self.future.poll(cx)? {
            return Ok(Async::Ready(item));
        }
        if let Async::Ready(()) = self.timer.poll(cx).map_err(|never| match never {})? {
            let ot = self.on_timeout.take().expect("polled withtimeout after completion");
            let item = (ot)()?;
            return Ok(Async::Ready(item));
        }
        Ok(Async::Pending)
    }
}

/// The packet reciever for timers.
#[derive(Debug)]
struct TimerReceiver {
    task: AtomicWaker,
    did_fire: AtomicBool,
}

impl PacketReceiver for TimerReceiver {
    fn receive_packet(&self, packet: zx::Packet) {
        if let zx::PacketContents::SignalRep(signals) = packet.contents() {
            if signals.observed().contains(zx::Signals::TIMER_SIGNALED) {
                self.did_fire.store(true, Ordering::SeqCst);
                self.task.wake();
            }
        }
    }
}

/// An asynchronous timer.
#[derive(Debug)]
#[must_use = "futures do nothing unless polled"]
pub struct Timer<E> {
    timer: zx::Timer,
    timer_receiver: ReceiverRegistration<TimerReceiver>,
    _marker: PhantomData<E>
}

impl<E> Timer<E> {
    /// Create a new timer scheduled to fire at `time`.
    pub fn new(time: zx::Time) -> Result<Self, zx::Status> {
        let ehandle = EHandle::local();
        let timer_receiver = ehandle.register_receiver(
            Arc::new(TimerReceiver {
                task: AtomicWaker::new(),
                did_fire: AtomicBool::new(false),
            })
        );

        let timer = zx::Timer::create(zx::ClockId::Monotonic)?;

        timer.wait_async_handle(
            ehandle.port(),
            timer_receiver.key(),
            zx::Signals::TIMER_SIGNALED,
            zx::WaitAsyncOpts::Repeating,
        )?;

        timer.set(time, 0.nanos())?;

        Ok(Timer {
            timer,
            timer_receiver,
            _marker: PhantomData,
        })
    }

    /// Reset the `Timer` to a fire at a new time.
    pub fn reset(&mut self, time: zx::Time) -> Result<(), zx::Status> {
        self.timer_receiver.receiver().did_fire.store(false, Ordering::SeqCst);
        self.timer.set(time, 0.nanos())
    }

    fn did_fire(&self) -> bool {
        self.timer_receiver.receiver().did_fire.load(Ordering::SeqCst)
    }

    fn register_task(&self, cx: &mut task::Context) {
        self.timer_receiver.receiver().task.register(cx.waker());
    }
}

impl<E> Future for Timer<E> {
    type Item = ();
    type Error = E;
    fn poll(&mut self, cx: &mut task::Context) -> Poll<(), E> {
        if self.did_fire() {
            Ok(Async::Ready(()))
        } else {
            self.register_task(cx);
            Ok(Async::Pending)
        }
    }
}

/// An asynchronous interval timer.
/// This is a stream of events resolving at a rate of once-per interval.
#[derive(Debug)]
#[must_use = "streams do nothing unless polled"]
pub struct Interval {
    timer: Timer<zx::Status>,
    next: zx::Time,
    duration: zx::Duration,
}

impl Interval {
    /// Create a new `Interval` which yields every `duration`.
    pub fn new(duration: zx::Duration) -> Result<Self, zx::Status> {
        let next = duration.after_now();
        Ok(Interval {
            timer: Timer::new(next)?,
            next,
            duration,
        })
    }

    /// Reset the `Interval` to a fire at a new rate.
    pub fn reset(&mut self, duration: zx::Duration) -> Result<(), zx::Status> {
        self.duration = duration;
        let new_next = duration.after_now();
        self.next = new_next;
        self.timer.reset(new_next)
    }
}

impl Stream for Interval {
    type Item = ();
    type Error = zx::Status;
    fn poll_next(&mut self, cx: &mut task::Context) -> Poll<Option<()>, Self::Error> {
        match self.timer.poll(cx) {
            Ok(Async::Ready(())) => {
                self.timer.register_task(cx);
                self.next += self.duration;
                self.timer.reset(self.next)?;
                Ok(Async::Ready(Some(())))
            }
            Ok(Async::Pending) => {
                self.timer.register_task(cx);
                Ok(Async::Pending)
            }
            Err(e) => Err(e),
        }
    }
}
