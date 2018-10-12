// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Testing-related utilities.

use std::collections::HashMap;

use rand::{SeedableRng, XorShiftRng};

use crate::device::{DeviceId, DeviceLayerEventDispatcher};
use crate::transport::udp::UdpEventDispatcher;
use crate::transport::TransportLayerEventDispatcher;
use crate::{Context, EventDispatcher, TimerId};

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

#[derive(Default)]
pub struct DummyEventDispatcher {
    frames_sent: Vec<(DeviceId, Vec<u8>)>,
}

impl DummyEventDispatcher {
    pub fn frames_sent(&self) -> &[(DeviceId, Vec<u8>)] {
        &self.frames_sent
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
    fn schedule_timeout(&mut self, _duration: std::time::Duration, _id: TimerId) -> Option<std::time::Instant> {
        unimplemented!()
    }

    fn schedule_timeout_instant(&mut self, _time: std::time::Instant, _id: TimerId) -> Option<std::time::Instant> {
        unimplemented!()
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<std::time::Instant> {
        unimplemented!()
    }
}
