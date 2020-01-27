// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Schedule pings when they need to be scheduled, provide an estimation of round trip time

use crate::future_help::{log_errors, Observable, Observer};
use crate::runtime::{maybe_wait_until, spawn};
use anyhow::{format_err, Error};
use futures::{prelude::*, select};
use std::cell::RefCell;
use std::collections::{HashMap, VecDeque};
use std::convert::TryInto;
use std::pin::Pin;
use std::rc::Rc;
use std::task::{Context, Poll, Waker};
use std::time::{Duration, Instant};

const MIN_PING_SPACING: Duration = Duration::from_millis(100);
const MAX_PING_SPACING: Duration = Duration::from_secs(20);
const MAX_SAMPLE_AGE: Duration = Duration::from_secs(2 * 60);
const MAX_PING_AGE: Duration = Duration::from_secs(15);

/// A pong record includes an id and the amount of time taken to schedule the pong
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Pong {
    /// The requestors ping id
    pub id: u64,
    /// The queue time (in microseconds) before responding
    pub queue_time: u64,
}

#[derive(Debug)]
struct Sample {
    when: Instant,
    rtt_us: i64,
}

struct State {
    round_trip_time: Observable<Option<Duration>>,
    send_ping: bool,
    on_send_ping: Option<Waker>,
    sent_ping: Option<(u64, Instant)>,
    on_sent_ping: Option<Waker>,
    send_pong: Option<(u64, Instant)>,
    on_send_pong: Option<Waker>,
    received_pong: Option<(Pong, Instant)>,
    on_received_pong: Option<Waker>,
    closed: bool,
    on_closed: Option<Waker>,
    next_ping_id: u64,
}

struct OnSentPing<'a>(&'a RefCell<State>);
impl<'a> Future for OnSentPing<'a> {
    type Output = (u64, Instant);
    fn poll(self: Pin<&mut Self>, ctx: &mut std::task::Context<'_>) -> Poll<Self::Output> {
        let mut inner = Pin::into_inner(self).0.borrow_mut();
        match inner.sent_ping.take() {
            Some(r) => Poll::Ready(r),
            None => {
                inner.on_sent_ping = Some(ctx.waker().clone());
                Poll::Pending
            }
        }
    }
}

struct OnReceivedPong<'a>(&'a RefCell<State>);
impl<'a> Future for OnReceivedPong<'a> {
    type Output = (Pong, Instant);
    fn poll(self: Pin<&mut Self>, ctx: &mut std::task::Context<'_>) -> Poll<Self::Output> {
        let mut inner = Pin::into_inner(self).0.borrow_mut();
        match inner.received_pong.take() {
            Some(r) => Poll::Ready(r),
            None => {
                inner.on_received_pong = Some(ctx.waker().clone());
                Poll::Pending
            }
        }
    }
}

struct Closed<'a>(&'a RefCell<State>);
impl<'a> Future for Closed<'a> {
    type Output = ();
    fn poll(self: Pin<&mut Self>, ctx: &mut std::task::Context<'_>) -> Poll<Self::Output> {
        let mut inner = Pin::into_inner(self).0.borrow_mut();
        if inner.closed {
            Poll::Ready(())
        } else {
            inner.on_closed = Some(ctx.waker().clone());
            Poll::Pending
        }
    }
}

async fn ping_pong(state: Rc<RefCell<State>>) -> Result<(), Error> {
    let mut samples = VecDeque::<Sample>::new();
    let mut variance = 0i64;
    let mut sent_ping_map = HashMap::<u64, Instant>::new();
    let mut sent_ping_list = VecDeque::<(u64, Instant)>::new();
    let mut ping_spacing = Duration::from_millis(100);
    let mut last_ping_sent = Instant::now();
    let mut timeout = None;
    let mut published_mean = 0i64;
    loop {
        #[derive(Debug)]
        enum Action {
            Timeout,
            SentPing(u64, Instant),
            ReceivedPong(Pong, Instant),
        };
        // Wait for event
        let action = select! {
            _ = maybe_wait_until(timeout).fuse() => Action::Timeout,
            p = OnSentPing(&*state).fuse() => Action::SentPing(p.0, p.1),
            p = OnReceivedPong(&*state).fuse() => Action::ReceivedPong(p.0, p.1),
            _ = Closed(&*state).fuse() => return Ok(()),
        };
        // Perform selected action
        let variance_before = variance;
        let now = Instant::now();
        let mut state = state.borrow_mut();
        match action {
            Action::Timeout => {
                timeout = None;
                if let Some(epoch) = now.checked_sub(MAX_SAMPLE_AGE) {
                    while samples.len() > 3 && samples[0].when < epoch {
                        samples.pop_front();
                    }
                }
                if let Some(epoch) = now.checked_sub(MAX_PING_AGE) {
                    while sent_ping_list.len() > 1 && sent_ping_list[0].1 < epoch {
                        sent_ping_map.remove(&sent_ping_list.pop_front().unwrap().0);
                    }
                }
                state.send_ping = true;
                state.on_send_ping.take().map(|w| w.wake());
            }
            Action::SentPing(id, when) => {
                last_ping_sent = when;
                sent_ping_map.insert(id, when);
                sent_ping_list.push_back((id, when));
                sent_ping_map.insert(id, when);
            }
            Action::ReceivedPong(Pong { id, queue_time }, when) => {
                if let Some(ping_sent) = sent_ping_map.get(&id) {
                    if let Some(rtt) =
                        (now - *ping_sent).checked_sub(Duration::from_micros(queue_time))
                    {
                        samples.push_back(Sample {
                            when,
                            rtt_us: rtt.as_micros().try_into().unwrap_or(std::i64::MAX),
                        });
                    }
                }
            }
        }
        // Update statistics
        let mut mean = 0i64;
        let mut total = 0i64;
        let n = samples.len() as i64;
        if n == 0i64 {
            variance = 0;
        } else {
            for Sample { rtt_us, .. } in samples.iter() {
                total = total
                    .checked_add(*rtt_us)
                    .ok_or_else(|| format_err!("Overflow calculating mean"))?;
            }
            mean = total / n;
            if n == 1i64 {
                variance = 0;
            } else {
                let mut numer = 0;
                for Sample { rtt_us, .. } in samples.iter() {
                    numer += square(rtt_us - mean)
                        .ok_or_else(|| format_err!("Overflow calculating variance"))?;
                }
                variance = numer / (n - 1i64);
            }
        }
        // Publish state
        if variance > variance_before {
            ping_spacing /= 2;
            if ping_spacing < MIN_PING_SPACING {
                ping_spacing = MIN_PING_SPACING;
            }
        } else if variance < variance_before {
            ping_spacing = ping_spacing * 5 / 4;
            if ping_spacing > MAX_PING_SPACING {
                ping_spacing = MAX_PING_SPACING;
            }
        }

        let new_round_trip_time = mean > 0
            && (published_mean <= 0 || (published_mean - mean).abs() > published_mean / 10);
        if new_round_trip_time {
            published_mean = mean;
            eprintln!("PUBLISH MEAN: {} variance={}", mean, variance);
            state.round_trip_time.push(Some(Duration::from_micros(mean as u64)));
        }

        if !state.send_ping {
            timeout = Some(last_ping_sent + ping_spacing);
        }
    }
}

/// Schedule pings when they need to be scheduled, provide an estimation of round trip time
pub struct PingTracker(Rc<RefCell<State>>);

fn square(a: i64) -> Option<i64> {
    a.checked_mul(a)
}

impl Drop for PingTracker {
    fn drop(&mut self) {
        let mut state = self.0.borrow_mut();
        state.closed = true;
        state.on_closed.take().map(|w| w.wake());
    }
}

impl PingTracker {
    /// Setup a new (empty) PingTracker
    pub fn new() -> PingTracker {
        let state = Rc::new(RefCell::new(State {
            round_trip_time: Observable::new(None),
            send_ping: true,
            on_send_ping: None,
            sent_ping: None,
            on_sent_ping: None,
            send_pong: None,
            on_send_pong: None,
            received_pong: None,
            on_received_pong: None,
            closed: false,
            on_closed: None,
            next_ping_id: 1,
        }));
        spawn(log_errors(ping_pong(state.clone()), "Failed ping pong"));
        Self(state)
    }

    /// Query current round trip time
    pub fn new_round_trip_time_observer(&self) -> Observer<Option<Duration>> {
        self.0.borrow().round_trip_time.new_observer()
    }

    pub fn round_trip_time(&self) -> Option<Duration> {
        self.0.borrow().round_trip_time.current()
    }

    pub fn take_send_ping(&self, ctx: &mut Context<'_>) -> Option<u64> {
        let mut state = self.0.borrow_mut();
        if state.send_ping {
            state.send_ping = false;
            let id = state.next_ping_id;
            state.next_ping_id += 1;
            let now = Instant::now();
            state.sent_ping = Some((id, now));
            state.on_sent_ping.take().map(|w| w.wake());
            Some(id)
        } else {
            state.on_send_ping = Some(ctx.waker().clone());
            None
        }
    }

    pub fn take_send_pong(&self, ctx: &mut Context<'_>) -> Option<Pong> {
        let mut state = self.0.borrow_mut();
        if let Some((id, ping_received)) = state.send_pong.take() {
            let queue_time =
                (Instant::now() - ping_received).as_micros().try_into().unwrap_or(std::u64::MAX);
            Some(Pong { id, queue_time })
        } else {
            state.on_send_pong = Some(ctx.waker().clone());
            None
        }
    }

    /// Upon receiving a pong: return a set of operations that need to be scheduled
    pub fn got_pong(&self, pong: Pong) {
        let mut state = self.0.borrow_mut();
        state.received_pong = Some((pong, Instant::now()));
        state.on_received_pong.take().map(|w| w.wake());
    }

    pub fn got_ping(&self, ping: u64) {
        let mut state = self.0.borrow_mut();
        state.send_pong = Some((ping, Instant::now()));
        state.on_send_pong.take().map(|w| w.wake());
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::router::test_util::run;
    use crate::runtime::wait_until;
    use futures::task::noop_waker;

    #[test]
    fn published_mean_updates() {
        run(|| {
            async move {
                let pt = PingTracker::new();
                let mut rtt_obs = pt.new_round_trip_time_observer();
                assert_eq!(pt.take_send_ping(&mut Context::from_waker(&noop_waker())), Some(1));
                assert_eq!(rtt_obs.next().await, Some(None));
                wait_until(Instant::now() + Duration::from_secs(1)).await;
                pt.got_pong(Pong { id: 1, queue_time: 100 });
                let next = rtt_obs.next().await;
                assert_ne!(next, None);
                assert_ne!(next, Some(None));
            }
        })
    }
}
