// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Testing-related utilities.

use std::collections::HashMap;
use std::time::{Duration, Instant};

use rand::{SeedableRng, XorShiftRng};

use crate::device::{DeviceId, DeviceLayerEventDispatcher};
use crate::transport::udp::UdpEventDispatcher;
use crate::transport::TransportLayerEventDispatcher;
use crate::{handle_timeout, Context, EventDispatcher, TimerId};

/// Create a new deterministic RNG from a seed.
pub fn new_rng(mut seed: u64) -> impl SeedableRng<[u32; 4]> {
    if seed == 0 {
        // XorShiftRng can't take 0 seeds
        seed = 1;
    }
    XorShiftRng::from_seed([
        seed as u32,
        (seed >> 32) as u32,
        seed as u32,
        (seed >> 32) as u32,
    ])
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
    let event = ctx.dispatcher.timer_events.iter().min_by_key(|e| e.0);
    if let Some(event) = event {
        let (t, id) = ctx.dispatcher.timer_events.remove(
            ctx.dispatcher
                .timer_events
                .iter()
                .position(|x| x == event)
                .unwrap(),
        );
        ctx.dispatcher.current_time = t;
        handle_timeout(ctx, id);
        true
    } else {
        false
    }
}

pub struct DummyEventDispatcher {
    frames_sent: Vec<(DeviceId, Vec<u8>)>,
    // Currently, this list is unordered, and we scan through the entire list whenever we use it.
    // TODO(wesleyac) Maybe use a btree?
    timer_events: Vec<(Instant, TimerId)>,
    current_time: Instant,
}

impl DummyEventDispatcher {
    pub fn frames_sent(&self) -> &[(DeviceId, Vec<u8>)] {
        &self.frames_sent
    }

    /// Get an unordered list of all scheduled timer events
    pub fn timer_events(&self) -> &[(Instant, TimerId)] {
        &self.timer_events
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
            timer_events: vec![],
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
        self.timer_events.push((time, id));
        ret
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<Instant> {
        // There is the invariant that there can only be one timer event per TimerId, so we only
        // need to remove at most one element from timer_events.
        Some(
            self.timer_events
                .remove(self.timer_events.iter().position(|x| x.1 == id)?)
                .0,
        )
    }
}
