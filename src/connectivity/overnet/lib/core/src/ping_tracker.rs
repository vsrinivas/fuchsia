// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Schedule pings when they need to be scheduled, provide an estimation of round trip time

use std::collections::{HashMap, VecDeque};
use std::convert::TryInto;
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

/// Schedule pings when they need to be scheduled, provide an estimation of round trip time
pub struct PingTracker {
    round_trip_time: Option<Duration>,
    send_ping: bool,
    send_pong: Option<(u64, Instant)>,
    next_ping_id: u64,
    samples: VecDeque<Sample>,
    variance: i64,
    sent_ping_map: HashMap<u64, Instant>,
    sent_ping_list: VecDeque<(u64, Instant)>,
    ping_spacing: Duration,
    last_ping_sent: Instant,
}

fn square(a: i64) -> Option<i64> {
    a.checked_mul(a)
}

impl PingTracker {
    /// Setup a new (empty) PingTracker
    pub fn new() -> PingTracker {
        let ping_spacing = Duration::from_millis(100);
        Self {
            round_trip_time: None,
            send_ping: true,
            send_pong: None,
            next_ping_id: 1,
            samples: VecDeque::new(),
            variance: 0i64,
            sent_ping_map: HashMap::new(),
            sent_ping_list: VecDeque::new(),
            ping_spacing,
            last_ping_sent: Instant::now() - ping_spacing,
        }
    }

    pub fn round_trip_time(&self) -> Option<Duration> {
        self.round_trip_time
    }

    pub fn on_received_frame(&mut self, ping: Option<u64>, pong: Option<Pong>) {
        let now = Instant::now();
        if let Some(Pong { id, queue_time }) = pong {
            if let Some(rtt) = self.sent_ping_map.get(&id).and_then(|ping_sent| {
                (now - *ping_sent).checked_sub(Duration::from_micros(queue_time))
            }) {
                self.samples.push_back(Sample {
                    when: now,
                    rtt_us: rtt.as_micros().try_into().unwrap_or(std::i64::MAX),
                });
            }
        }
        if let Some(ping) = ping {
            self.send_pong = Some((ping, now));
        }
        self.recalculate();
    }

    pub fn needs_send(&self) -> bool {
        self.send_ping || self.send_pong.is_some()
    }

    pub fn pull_send(&mut self) -> (Option<u64>, Option<Pong>) {
        let out = (
            if std::mem::replace(&mut self.send_ping, false) {
                let id = self.next_ping_id;
                self.next_ping_id += 1;
                let now = Instant::now();
                self.last_ping_sent = now;
                self.sent_ping_map.insert(id, now);
                self.sent_ping_list.push_back((id, now));
                self.sent_ping_map.insert(id, now);
                Some(id)
            } else {
                None
            },
            self.send_pong.take().map(|(id, ping_received)| {
                let queue_time = (Instant::now() - ping_received)
                    .as_micros()
                    .try_into()
                    .unwrap_or(std::u64::MAX);
                Pong { id, queue_time }
            }),
        );
        if out.0.is_some() || out.1.is_some() {
            self.recalculate();
        }
        out
    }

    pub fn next_timeout(&self) -> Option<Instant> {
        if self.send_ping {
            None
        } else {
            Some(self.last_ping_sent + self.ping_spacing)
        }
    }

    pub fn on_timeout(&mut self) {
        self.send_ping = true;
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
        self.recalculate();
    }

    fn recalculate(&mut self) {
        self.round_trip_time = (|| {
            let variance_before = self.variance;
            let mut mean = 0i64;
            let mut total = 0i64;
            let n = self.samples.len() as i64;
            if n == 0i64 {
                self.variance = 0;
            } else {
                for Sample { rtt_us, .. } in self.samples.iter() {
                    total = total.checked_add(*rtt_us)?;
                }
                mean = total / n;
                if n == 1i64 {
                    self.variance = 0;
                } else {
                    let mut numer = 0i64;
                    for Sample { rtt_us, .. } in self.samples.iter() {
                        numer = numer.checked_add(square(rtt_us - mean)?)?;
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
            if mean > 0 {
                Some(Duration::from_micros(mean as u64))
            } else {
                None
            }
        })();
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia::test]
    fn published_mean_updates() {
        let mut pt = PingTracker::new();
        assert!(pt.needs_send());
        assert_eq!(pt.round_trip_time(), None);
        let (ping, pong) = pt.pull_send();
        assert_eq!(pt.round_trip_time(), None);
        assert_eq!(pong, None);
        assert_eq!(ping, Some(1));
        std::thread::sleep(Duration::from_secs(1));
        pt.on_received_frame(None, Some(Pong { id: 1, queue_time: 100 }));
        assert_ne!(pt.round_trip_time(), None);
    }
}
