// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Schedule pings when they need to be scheduled, provide an estimation of round trip time

use std::collections::{HashMap, VecDeque};
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

/// Structure returning actions that need to be scheduled in response to a PingTracker operation
#[must_use = "This result may contain actions that users need to act upon"]
#[derive(Debug, PartialEq)]
pub struct PingTrackerResult {
    pub sched_timeout: Option<Duration>,
    pub sched_send: bool,
    pub new_round_trip_time: bool,
}

#[derive(Debug)]
struct Sample {
    when: Instant,
    rtt_us: i64,
}

/// Schedule pings when they need to be scheduled, provide an estimation of round trip time
#[derive(Debug)]
pub struct PingTracker {
    samples: VecDeque<Sample>,
    mean: i64,
    variance: i64,
    sent_ping_map: HashMap<u64, Instant>,
    sent_ping_list: VecDeque<(u64, Instant)>,
    ping_spacing: Duration,
    used_ping_spacing: bool,
    last_ping_sent: Option<Instant>,
    scheduled_timeout: Option<Instant>,
    scheduled_send: bool,
    next_ping_id: u64,
    published_mean: i64,
}

fn square(a: i64) -> Option<i64> {
    a.checked_mul(a)
}

impl PingTracker {
    /// Setup a new (empty) PingTracker
    pub fn new() -> (PingTracker, PingTrackerResult) {
        let tracker = PingTracker {
            samples: VecDeque::new(),
            mean: 0,
            variance: 0,
            sent_ping_map: HashMap::new(),
            sent_ping_list: VecDeque::new(),
            ping_spacing: Duration::from_millis(100),
            used_ping_spacing: true,
            last_ping_sent: None,
            scheduled_send: true,
            scheduled_timeout: None,
            next_ping_id: 1,
            published_mean: 0,
        };
        let ptres =
            PingTrackerResult { sched_send: true, sched_timeout: None, new_round_trip_time: false };
        (tracker, ptres)
    }

    /// Query current round trip time
    pub fn round_trip_time(&self) -> Option<Duration> {
        if self.mean > 0 {
            Some(Duration::from_micros(self.mean as u64))
        } else {
            None
        }
    }

    fn send_ping(&mut self, now: Instant) -> Option<u64> {
        self.last_ping_sent = Some(now);
        let id = self.next_ping_id;
        self.next_ping_id += 1;
        self.sent_ping_map.insert(id, now);
        self.sent_ping_list.push_back((id, now));
        Some(id)
    }

    /// Returns a ping id if a ping needs to be sent, updating tracking to indicate that the ping
    /// was sent.
    pub fn maybe_send_ping(
        &mut self,
        now: Instant,
        empty_packet: bool,
    ) -> (Option<u64>, PingTrackerResult) {
        self.mutate(now, |this| {
            log::trace!(
                "MAYBE SEND PING: empty_packet:{} last_ping_sent:{:?} ping_spacing:{:?} now:{:?}",
                empty_packet,
                this.last_ping_sent,
                this.ping_spacing,
                now
            );
            if empty_packet {
                this.scheduled_send = false;
            }
            if let Some(last_ping_sent) = this.last_ping_sent {
                if now >= last_ping_sent + this.ping_spacing {
                    this.send_ping(now)
                } else {
                    None
                }
            } else {
                this.send_ping(now)
            }
        })
    }

    /// Upon timeout: check whether that timeout causes a ping to need to be scheduled
    pub fn on_timeout(&mut self, now: Instant) -> PingTrackerResult {
        self.mutate(now, |this| {
            log::trace!(
                "TIMEOUT: now:{:?} samples:{:?} sent_ping_list:{:?} sent_ping_map:{:?}",
                now,
                this.samples,
                this.sent_ping_list,
                this.sent_ping_map
            );
            this.scheduled_timeout = None;
            if let Some(epoch) = now.checked_sub(MAX_SAMPLE_AGE) {
                while this.samples.len() > 3 && this.samples[0].when < epoch {
                    this.samples.pop_front();
                }
            }
            if let Some(epoch) = now.checked_sub(MAX_PING_AGE) {
                while this.sent_ping_list.len() > 1 && this.sent_ping_list[0].1 < epoch {
                    this.sent_ping_map.remove(&this.sent_ping_list.pop_front().unwrap().0);
                }
            }
        })
        .1
    }

    /// Upon receiving a pong: return a set of operations that need to be scheduled
    pub fn got_pong(&mut self, now: Instant, pong: Pong) -> PingTrackerResult {
        self.mutate(now, |this| {
            log::trace!(
                "GOT_PONG: {:?} now:{:?} sent_ping_map:{:?}",
                pong,
                now,
                this.sent_ping_map
            );
            if let Some(send_time) = this.sent_ping_map.remove(&pong.id) {
                this.samples
                    .push_back(Sample { when: now, rtt_us: (now - send_time).as_micros() as i64 });
            }
        })
        .1
    }

    fn mutate<R>(
        &mut self,
        now: Instant,
        f: impl FnOnce(&mut Self) -> R,
    ) -> (R, PingTrackerResult) {
        log::trace!("BEGIN MUTATE");

        let variance_before = self.variance;
        let r = f(self);
        if let Err(e) = self.recalculate_stats() {
            log::trace!("Failed to recalculate statistics: {}", e);
            self.mean = 0;
            self.variance = 0;
        }
        let variance_after = self.variance;

        log::trace!("END MUTATE: variance:{}->{} ping_spacing:{:?} mean:{} scheduled_send:{} scheduled_timeout:{:?}", variance_before, variance_after, self.ping_spacing, self.mean, self.scheduled_send, self.scheduled_timeout);

        if self.used_ping_spacing {
            if variance_after > variance_before {
                self.ping_spacing /= 2;
                if self.ping_spacing < MIN_PING_SPACING {
                    self.ping_spacing = MIN_PING_SPACING;
                    self.used_ping_spacing = false;
                }
            } else if variance_after < variance_before {
                self.ping_spacing = self.ping_spacing * 5 / 4;
                if self.ping_spacing > MAX_PING_SPACING {
                    self.ping_spacing = MAX_PING_SPACING;
                    self.used_ping_spacing = false;
                }
            }
        }

        let new_round_trip_time = self.mean > 0
            && (self.published_mean <= 0
                || (self.published_mean - self.mean).abs() > self.published_mean / 10);
        if new_round_trip_time {
            self.published_mean = self.mean;
        }

        let mut sched_send = !self.scheduled_send
            && self.last_ping_sent.map_or(true, |tm| now >= tm + self.ping_spacing);

        let want_timeout = self.last_ping_sent.unwrap_or(now) + 2 * self.ping_spacing;
        let sched_timeout = if self.scheduled_timeout.map_or(true, |tm| tm > want_timeout) {
            self.scheduled_timeout = Some(want_timeout);
            if want_timeout > now {
                Some(want_timeout - now)
            } else {
                sched_send = true;
                None
            }
        } else {
            None
        };

        if sched_send {
            self.scheduled_send = true;
        }

        (r, PingTrackerResult { sched_send, sched_timeout, new_round_trip_time })
    }

    fn recalculate_stats(&mut self) -> Result<(), &'static str> {
        log::trace!(
            "RECALCSTATS: {:?}",
            self.samples.iter().map(|x| x.rtt_us).collect::<Vec<i64>>()
        );

        let mut total = 0i64;
        let n = self.samples.len() as i64;
        if n == 0 {
            self.mean = 0;
            self.variance = 0;
            return Ok(());
        }
        for Sample { rtt_us, .. } in self.samples.iter() {
            total = total.checked_add(*rtt_us).ok_or("Overflow calculating mean")?;
        }
        let mean = total / n;
        self.mean = mean;

        if n == 1 {
            self.variance = 0;
            return Ok(());
        }

        let mut numer = 0;
        for Sample { rtt_us, .. } in self.samples.iter() {
            numer += square(rtt_us - mean).ok_or("Overflow calculating variance")?;
        }
        let variance = numer / (n - 1);

        self.variance = variance;

        Ok(())
    }
}

/// Track which pings need pongs to be sent
#[derive(Debug)]
pub struct PongTracker {
    pong: Option<(u64, Instant)>,
}

impl PongTracker {
    /// Setup a new pong tracker
    pub fn new() -> PongTracker {
        PongTracker { pong: None }
    }

    /// Returns a Pong structure if a pong needs to be sent, updating tracking to indicate that the
    /// pong was sent.
    pub fn maybe_send_pong(&mut self) -> Option<Pong> {
        self.pong.take().map(|(id, received)| Pong {
            id,
            queue_time: (Instant::now() - received).as_micros() as u64,
        })
    }

    /// Register that a ping was received, and return true if a pong should be sent at the next
    /// available opportunity
    pub fn got_ping(&mut self, id: u64) -> bool {
        self.pong.replace((id, Instant::now())).is_none()
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn create() {
        assert_eq!(
            PingTracker::new().1,
            PingTrackerResult { sched_send: true, new_round_trip_time: false, sched_timeout: None }
        );
    }

    #[test]
    fn published_mean_updates() {
        let (mut pt, _) = PingTracker::new();
        let now = Instant::now();
        let id = pt.send_ping(now).unwrap();
        let r = pt.got_pong(now + Duration::from_millis(100), Pong { id, queue_time: 100 });
        assert!(r.new_round_trip_time);
        assert_ne!(pt.published_mean, 0);
    }
}
