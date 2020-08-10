// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Schedule pings when they need to be scheduled, provide an estimation of round trip time

use crate::future_help::{Observable, Observer, PollMutex};
use anyhow::{format_err, Error};
use fuchsia_async::Timer;
use futures::{
    future::{poll_fn, Either},
    lock::{Mutex, MutexGuard},
    prelude::*,
    ready,
};
use std::collections::{HashMap, VecDeque};
use std::convert::TryInto;
use std::pin::Pin;
use std::sync::Arc;
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
    send_pong: Option<(u64, Instant)>,
    on_send_ping_pong: Option<Waker>,
    sent_ping: Option<(u64, Instant)>,
    on_sent_ping: Option<Waker>,
    received_pong: Option<(Pong, Instant)>,
    on_received_pong: Option<Waker>,
    next_ping_id: u64,
    samples: VecDeque<Sample>,
    variance: i64,
    sent_ping_map: HashMap<u64, Instant>,
    sent_ping_list: VecDeque<(u64, Instant)>,
    ping_spacing: Duration,
    last_ping_sent: Instant,
    timeout: Option<Instant>,
    wake_on_change_timeout: Option<Waker>,
    published_mean: i64,
}

struct OnSentPing<'a>(PollMutex<'a, State>);
impl<'a> Future for OnSentPing<'a> {
    type Output = (MutexGuard<'a, State>, u64, Instant);
    fn poll(mut self: Pin<&mut Self>, ctx: &mut std::task::Context<'_>) -> Poll<Self::Output> {
        let mut inner = ready!(self.0.poll(ctx));
        match inner.sent_ping.take() {
            Some(r) => Poll::Ready((inner, r.0, r.1)),
            None => {
                inner.on_sent_ping = Some(ctx.waker().clone());
                Poll::Pending
            }
        }
    }
}

struct OnReceivedPong<'a>(PollMutex<'a, State>);
impl<'a> Future for OnReceivedPong<'a> {
    type Output = (MutexGuard<'a, State>, Pong, Instant);
    fn poll(mut self: Pin<&mut Self>, ctx: &mut std::task::Context<'_>) -> Poll<Self::Output> {
        let mut inner = ready!(self.0.poll(ctx));
        match inner.received_pong.take() {
            Some(r) => Poll::Ready((inner, r.0, r.1)),
            None => {
                inner.on_received_pong = Some(ctx.waker().clone());
                Poll::Pending
            }
        }
    }
}

impl State {
    async fn recalculate(&mut self) -> Result<(), Error> {
        let variance_before = self.variance;
        let mut mean = 0i64;
        let mut total = 0i64;
        let n = self.samples.len() as i64;
        if n == 0i64 {
            self.variance = 0;
        } else {
            for Sample { rtt_us, .. } in self.samples.iter() {
                total = total
                    .checked_add(*rtt_us)
                    .ok_or_else(|| format_err!("Overflow calculating mean"))?;
            }
            mean = total / n;
            if n == 1i64 {
                self.variance = 0;
            } else {
                let mut numer = 0;
                for Sample { rtt_us, .. } in self.samples.iter() {
                    numer += square(rtt_us - mean)
                        .ok_or_else(|| format_err!("Overflow calculating variance"))?;
                }
                self.variance = numer / (n - 1i64);
            }
        }
        // Publish state
        if self.variance > variance_before {
            self.ping_spacing /= 2;
            if self.ping_spacing < MIN_PING_SPACING {
                self.ping_spacing = MIN_PING_SPACING;
            }
        } else if self.variance < variance_before {
            self.ping_spacing = self.ping_spacing * 5 / 4;
            if self.ping_spacing > MAX_PING_SPACING {
                self.ping_spacing = MAX_PING_SPACING;
            }
        }
        let new_round_trip_time = mean > 0
            && (self.published_mean <= 0
                || (self.published_mean - mean).abs() > self.published_mean / 10);
        if new_round_trip_time {
            self.published_mean = mean;
            self.round_trip_time.push(Some(Duration::from_micros(mean as u64))).await;
        }
        if !self.send_ping {
            let new_timeout = Some(self.last_ping_sent + self.ping_spacing);
            if new_timeout != self.timeout {
                self.timeout = new_timeout;
                self.wake_on_change_timeout.take().map(|w| w.wake());
            }
        }
        Ok(())
    }

    fn garbage_collect(&mut self) {
        let now = Instant::now();
        if let Some(epoch) = now.checked_sub(MAX_SAMPLE_AGE) {
            while self.samples.len() > 3 && self.samples[0].when < epoch {
                self.samples.pop_front();
            }
        }
        if let Some(epoch) = now.checked_sub(MAX_PING_AGE) {
            while self.sent_ping_list.len() > 1 && self.sent_ping_list[0].1 < epoch {
                self.sent_ping_map.remove(&self.sent_ping_list.pop_front().unwrap().0);
            }
        }
    }
}

async fn ping_pong(state: Arc<Mutex<State>>) -> Result<(), Error> {
    let state_ref: &Mutex<State> = &*state;

    futures::future::try_join3(
        async move {
            loop {
                let (mut state, id, when) = OnSentPing(PollMutex::new(state_ref)).await;
                state.last_ping_sent = when;
                state.sent_ping_map.insert(id, when);
                state.sent_ping_list.push_back((id, when));
                state.sent_ping_map.insert(id, when);
                state.recalculate().await?;
            }
        },
        async move {
            loop {
                let (mut state, Pong { id, queue_time }, when) =
                    OnReceivedPong(PollMutex::new(state_ref)).await;
                let now = Instant::now();
                if let Some(ping_sent) = state.sent_ping_map.get(&id) {
                    if let Some(rtt) =
                        (now - *ping_sent).checked_sub(Duration::from_micros(queue_time))
                    {
                        state.samples.push_back(Sample {
                            when,
                            rtt_us: rtt.as_micros().try_into().unwrap_or(std::i64::MAX),
                        });
                    }
                }
                state.recalculate().await?;
            }
        },
        async move {
            let mut state_lock = PollMutex::new(state_ref);
            const IDLE_TIMEOUT: Duration = Duration::from_secs(30);
            let timer_for_timeout = move |timeout: Option<Instant>| {
                Timer::new(timeout.unwrap_or_else(|| Instant::now() + IDLE_TIMEOUT))
            };
            let mut current_timeout = None;
            let mut timeout_fut = timer_for_timeout(current_timeout);
            loop {
                let mut poll_timeout = |ctx: &mut Context<'_>, current_timeout: Option<Instant>| {
                    let mut stats = ready!(state_lock.poll(ctx));
                    if stats.timeout != current_timeout {
                        Poll::Ready(stats.timeout)
                    } else {
                        stats.wake_on_change_timeout = Some(ctx.waker().clone());
                        Poll::Pending
                    }
                };
                match futures::future::select(
                    poll_fn(|ctx| poll_timeout(ctx, current_timeout)),
                    &mut timeout_fut,
                )
                .await
                {
                    Either::Left((timeout, _)) => {
                        current_timeout = timeout;
                        timeout_fut = timer_for_timeout(current_timeout);
                    }
                    Either::Right(_) => {
                        let mut state = state_lock.lock().await;
                        state.timeout = None;
                        timeout_fut = timer_for_timeout(None);
                        state.garbage_collect();
                        state.send_ping = true;
                        state.on_send_ping_pong.take().map(|w| w.wake());
                        state.recalculate().await?;
                    }
                }
            }
        },
    )
    .map_ok(|((), (), ())| ())
    .await
}

/// Schedule pings when they need to be scheduled, provide an estimation of round trip time
pub struct PingTracker(Arc<Mutex<State>>);

pub struct PingSender<'a>(PollMutex<'a, State>);

fn square(a: i64) -> Option<i64> {
    a.checked_mul(a)
}

impl PingTracker {
    /// Setup a new (empty) PingTracker
    pub fn new() -> (PingTracker, Observer<Option<Duration>>, Observer<Option<Duration>>) {
        let observable = Observable::new(None);
        let observer1 = observable.new_observer();
        let observer2 = observable.new_observer();
        let state = Arc::new(Mutex::new(State {
            round_trip_time: observable,
            send_ping: true,
            send_pong: None,
            on_send_ping_pong: None,
            sent_ping: None,
            on_sent_ping: None,
            received_pong: None,
            on_received_pong: None,
            next_ping_id: 1,
            samples: VecDeque::new(),
            variance: 0i64,
            sent_ping_map: HashMap::new(),
            sent_ping_list: VecDeque::new(),
            ping_spacing: Duration::from_millis(100),
            last_ping_sent: Instant::now(),
            timeout: None,
            wake_on_change_timeout: None,
            published_mean: 0i64,
        }));
        (Self(state), observer1, observer2)
    }

    pub fn run(&self) -> impl Future<Output = Result<(), Error>> {
        ping_pong(self.0.clone())
    }

    /// Upon receiving a pong: return a set of operations that need to be scheduled
    pub async fn got_pong(&self, pong: Pong) {
        let mut state = self.0.lock().await;
        state.received_pong = Some((pong, Instant::now()));
        state.on_received_pong.take().map(|w| w.wake());
    }

    pub async fn got_ping(&self, ping: u64) {
        let mut state = self.0.lock().await;
        state.send_pong = Some((ping, Instant::now()));
        state.on_send_ping_pong.take().map(|w| w.wake());
    }
}

impl<'a> PingSender<'a> {
    pub fn new(tracker: &'a PingTracker) -> Self {
        Self(PollMutex::new(&*tracker.0))
    }

    pub fn poll(&mut self, ctx: &mut Context<'_>) -> (Option<u64>, Option<Pong>) {
        let mut state = match self.0.poll(ctx) {
            Poll::Pending => return (None, None),
            Poll::Ready(x) => x,
        };
        let ping = if state.send_ping {
            state.send_ping = false;
            let id = state.next_ping_id;
            state.next_ping_id += 1;
            let now = Instant::now();
            state.sent_ping = Some((id, now));
            state.on_sent_ping.take().map(|w| w.wake());
            Some(id)
        } else {
            None
        };
        let pong = if let Some((id, ping_received)) = state.send_pong.take() {
            let queue_time =
                (Instant::now() - ping_received).as_micros().try_into().unwrap_or(std::u64::MAX);
            Some(Pong { id, queue_time })
        } else {
            None
        };
        if ping.is_none() && pong.is_none() {
            state.on_send_ping_pong = Some(ctx.waker().clone());
        }
        (ping, pong)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async::{Task, Timer};
    use futures::task::noop_waker;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn published_mean_updates(run: usize) {
        crate::test_util::init();
        let (pt, mut rtt_obs, _) = PingTracker::new();
        let pt_run = pt.run();
        let _pt_run = Task::spawn(async move {
            pt_run.await.unwrap();
        });
        loop {
            log::info!("published_mean_updates[{}]: send ping", run);
            let mut ping_sender = PingSender::new(&pt);
            let (ping, pong) = ping_sender.poll(&mut Context::from_waker(&noop_waker()));
            assert_eq!(pong, None);
            if ping.is_none() {
                log::info!("published_mean_updates[{}]: got none for ping, retry", run);
                drop(ping_sender);
                Timer::new(Duration::from_millis(10)).await;
                continue;
            }
            assert_eq!(ping, Some(1));
            break;
        }
        log::info!("published_mean_updates[{}]: wait for second observation", run);
        assert_eq!(rtt_obs.next().await, Some(None));
        log::info!("published_mean_updates[{}]: sleep some", run);
        Timer::new(Duration::from_secs(1)).await;
        log::info!("published_mean_updates[{}]: publish pong", run);
        pt.got_pong(Pong { id: 1, queue_time: 100 }).await;
        log::info!("published_mean_updates[{}]: wait for third observation", run);
        let next = rtt_obs.next().await;
        assert_ne!(next, None);
        assert_ne!(next, Some(None));
    }
}
