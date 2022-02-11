// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

// This mod implements a refresh rate counter that calculates the exponential moving average of the
// frame rate. See https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average.

const ALPHA: f32 = 0.6;

pub(crate) struct Counter {
    // Most recent frame timestamp
    last_sample_timestamp: zx::Time,

    // Stores the exponential moving average of the time between two frames, using the above
    // `ALPHA` as the weight.
    avg_time_delta_ns: f32,
}

pub(crate) struct Counts {
    pub sample_rate_hz: f32,
    pub sample_time_delta_ms: f32,
}

impl Counter {
    pub fn new() -> Counter {
        Counter {
            last_sample_timestamp: zx::Time::get_monotonic(),
            avg_time_delta_ns: 0.0,
        }
    }

    pub fn add(&mut self, timestamp: zx::Time) {
        let delta = timestamp - self.last_sample_timestamp;
        self.last_sample_timestamp = timestamp;

        let delta = delta.into_nanos() as f32;

        // This is arithmetically equivalent to:
        // ALPHA * delta + (1 - ALPHA) * self.avg_time_delta_ns
        self.avg_time_delta_ns += ALPHA * (delta - self.avg_time_delta_ns);
    }

    pub fn stats(&self) -> Counts {
        Counts {
            sample_rate_hz: 1000000000f32 / self.avg_time_delta_ns,
            sample_time_delta_ms: self.avg_time_delta_ns / 1000000f32,
        }
    }
}
