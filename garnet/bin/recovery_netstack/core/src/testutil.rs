// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Testing-related utilities.

use std::collections::{BTreeMap, HashMap};
use std::time::{Duration, Instant};

use byteorder::{ByteOrder, NativeEndian};
use rand::{SeedableRng, XorShiftRng};

use crate::device::{DeviceId, DeviceLayerEventDispatcher};
use crate::transport::udp::UdpEventDispatcher;
use crate::transport::TransportLayerEventDispatcher;
use crate::{handle_timeout, Context, EventDispatcher, TimerId};

/// Create a new deterministic RNG from a seed.
pub fn new_rng(mut seed: u64) -> XorShiftRng {
    if seed == 0 {
        // XorShiftRng can't take 0 seeds
        seed = 1;
    }
    let mut bytes = [0; 16];
    NativeEndian::write_u32(&mut bytes[0..4], seed as u32);
    NativeEndian::write_u32(&mut bytes[4..8], (seed >> 32) as u32);
    NativeEndian::write_u32(&mut bytes[8..12], seed as u32);
    NativeEndian::write_u32(&mut bytes[12..16], (seed >> 32) as u32);
    XorShiftRng::from_seed(bytes)
}

#[derive(Default, Debug)]
pub struct TestCounters {
    data: HashMap<String, usize>,
}

impl TestCounters {
    pub fn increment(&mut self, key: &str) {
        *(self.data.entry(key.to_string()).or_insert(0)) += 1;
    }

    pub fn get(&self, key: &str) -> &usize {
        self.data.get(key).unwrap_or(&0)
    }
}

/// log::Log implementation that uses stdout.
///
/// Useful when debugging tests.
struct Logger;

impl log::Log for Logger {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        true
    }

    fn log(&self, record: &log::Record) {
        println!("{}", record.args())
    }

    fn flush(&self) {}
}

static LOGGER: Logger = Logger;

/// Install a logger for tests.
///
/// Call this method at the beginning of the test for which logging is desired.  This function sets
/// global program state, so all tests that run after this function is called will use the logger.
pub fn set_logger_for_test() {
    log::set_logger(&LOGGER).unwrap();
    log::set_max_level(log::LevelFilter::Trace);
}

/// Skip current (fake) time forward to trigger the next timer event.
///
/// Returns true if a timer was triggered, false if there were no timers waiting to be
/// triggered.
pub fn trigger_next_timer(ctx: &mut Context<DummyEventDispatcher>) -> bool {
    match ctx
        .dispatcher
        .timer_events
        .keys()
        .next()
        .map(|t| *t)
        .and_then(|t| ctx.dispatcher.timer_events.remove(&t).map(|id| (t, id)))
    {
        Some((t, id)) => {
            ctx.dispatcher.current_time = t;
            handle_timeout(ctx, id);
            true
        }
        None => false,
    }
}

pub struct DummyEventDispatcher {
    frames_sent: Vec<(DeviceId, Vec<u8>)>,
    timer_events: BTreeMap<Instant, TimerId>,
    current_time: Instant,
}

impl DummyEventDispatcher {
    pub fn frames_sent(&self) -> &[(DeviceId, Vec<u8>)] {
        &self.frames_sent
    }

    /// Get an ordered list of all scheduled timer events
    pub fn timer_events<'a>(&'a self) -> impl Iterator<Item = (&'a Instant, &'a TimerId)> {
        self.timer_events.iter()
    }

    /// Get the current (fake) time
    pub fn current_time(self) -> Instant {
        self.current_time
    }
}

impl Default for DummyEventDispatcher {
    fn default() -> DummyEventDispatcher {
        DummyEventDispatcher {
            frames_sent: vec![],
            timer_events: BTreeMap::new(),
            current_time: Instant::now(),
        }
    }
}

impl UdpEventDispatcher for DummyEventDispatcher {
    type UdpConn = ();
    type UdpListener = ();
}

impl TransportLayerEventDispatcher for DummyEventDispatcher {}

impl DeviceLayerEventDispatcher for DummyEventDispatcher {
    fn send_frame(&mut self, device: DeviceId, frame: &[u8]) {
        self.frames_sent.push((device, frame.to_vec()));
    }
}

impl EventDispatcher for DummyEventDispatcher {
    fn schedule_timeout(&mut self, duration: Duration, id: TimerId) -> Option<Instant> {
        self.schedule_timeout_instant(self.current_time + duration, id)
    }

    fn schedule_timeout_instant(&mut self, time: Instant, id: TimerId) -> Option<Instant> {
        let ret = self.cancel_timeout(id);
        self.timer_events.insert(time, id);
        ret
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<Instant> {
        // There is the invariant that there can only be one timer event per TimerId, so we only
        // need to remove at most one element from timer_events.

        match self
            .timer_events
            .iter()
            .find_map(|(instant, event_timer_id)| {
                if *event_timer_id == id {
                    Some(*instant)
                } else {
                    None
                }
            }) {
            Some(instant) => {
                self.timer_events.remove(&instant);
                Some(instant)
            }
            None => None,
        }
    }
}
