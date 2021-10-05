// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::constants::COMPONENT_CPU_MAX_SAMPLES,
    fuchsia_inspect as inspect, fuchsia_zircon as zx,
    lazy_static::lazy_static,
    std::{
        collections::VecDeque,
        ops::{AddAssign, Deref, DerefMut, SubAssign},
    },
};

lazy_static! {
    static ref TIMESTAMP: inspect::StringReference<'static> = "timestamp".into();
    static ref CPU_TIME: inspect::StringReference<'static> = "cpu_time".into();
    static ref QUEUE_TIME: inspect::StringReference<'static> = "queue_time".into();
}

#[derive(Debug, Default)]
pub struct Measurement {
    timestamp: zx::Time,
    cpu_time: zx::Duration,
    queue_time: zx::Duration,
}

impl Measurement {
    /// An empty measurement (zeros) at the given `timestamp`.
    pub fn empty(timestamp: zx::Time) -> Self {
        Self {
            timestamp,
            cpu_time: zx::Duration::from_nanos(0),
            queue_time: zx::Duration::from_nanos(0),
        }
    }

    /// Records the measurement data to the given inspect `node`.
    pub fn record_to_node(&self, node: &inspect::Node) {
        node.record_int(&*TIMESTAMP, self.timestamp.into_nanos());
        node.record_int(&*CPU_TIME, self.cpu_time.into_nanos());
        node.record_int(&*QUEUE_TIME, self.queue_time.into_nanos());
    }

    /// The measured cpu time.
    pub fn cpu_time(&self) -> &zx::Duration {
        &self.cpu_time
    }

    /// The measured queue time.
    pub fn queue_time(&self) -> &zx::Duration {
        &self.queue_time
    }

    /// Time when the measurement was taken.
    pub fn timestamp(&self) -> &zx::Time {
        &self.timestamp
    }
}

impl AddAssign<&Measurement> for Measurement {
    fn add_assign(&mut self, other: &Measurement) {
        *self = Self {
            timestamp: self.timestamp,
            cpu_time: self.cpu_time + other.cpu_time,
            queue_time: self.queue_time + other.queue_time,
        };
    }
}

impl SubAssign<&Measurement> for Measurement {
    fn sub_assign(&mut self, other: &Measurement) {
        *self = Self {
            timestamp: self.timestamp,
            cpu_time: self.cpu_time - other.cpu_time,
            queue_time: self.queue_time - other.queue_time,
        };
    }
}

impl From<zx::TaskRuntimeInfo> for Measurement {
    fn from(info: zx::TaskRuntimeInfo) -> Self {
        Measurement::from_runtime_info(info, zx::Time::get_monotonic())
    }
}

impl Measurement {
    pub(crate) fn from_runtime_info(info: zx::TaskRuntimeInfo, timestamp: zx::Time) -> Self {
        Self {
            timestamp,
            cpu_time: zx::Duration::from_nanos(info.cpu_time),
            queue_time: zx::Duration::from_nanos(info.queue_time),
        }
    }
}

#[derive(Debug)]
pub struct MeasurementsQueue {
    values: VecDeque<Measurement>,
}

impl Deref for MeasurementsQueue {
    type Target = VecDeque<Measurement>;
    fn deref(&self) -> &Self::Target {
        &self.values
    }
}

impl DerefMut for MeasurementsQueue {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.values
    }
}

impl MeasurementsQueue {
    pub fn new() -> Self {
        Self { values: VecDeque::new() }
    }

    pub fn insert(&mut self, measurement: Measurement) {
        self.values.push_back(measurement);
        while self.values.len() > COMPONENT_CPU_MAX_SAMPLES {
            self.values.pop_front();
        }
    }
}
