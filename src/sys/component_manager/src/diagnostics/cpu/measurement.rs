// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::cpu::constants::{
        COMPONENT_CPU_MAX_SAMPLES, CPU_SAMPLE_PERIOD, MEASUREMENT_EPSILON,
    },
    core::cmp::Reverse,
    fuchsia_inspect as inspect, fuchsia_zircon as zx,
    injectable_time::TimeSource,
    lazy_static::lazy_static,
    std::cmp::max,
    std::cmp::{Eq, Ord, PartialEq, PartialOrd},
    std::collections::BinaryHeap,
    std::iter::DoubleEndedIterator,
    std::ops::{AddAssign, SubAssign},
    std::sync::Arc,
};

lazy_static! {
    static ref TIMESTAMP: inspect::StringReference<'static> = "timestamp".into();
    static ref CPU_TIME: inspect::StringReference<'static> = "cpu_time".into();
    static ref QUEUE_TIME: inspect::StringReference<'static> = "queue_time".into();
    static ref SAMPLES: inspect::StringReference<'static> = "@samples".into();
    static ref SAMPLE_INDEXES: Vec<inspect::StringReference<'static>> =
        (0..COMPONENT_CPU_MAX_SAMPLES).map(|x| x.to_string().into()).collect();
}

#[derive(Debug, Clone, Default, PartialOrd, Eq, Ord, PartialEq)]
pub struct Measurement {
    timestamp: zx::Time,
    cpu_time: zx::Duration,
    queue_time: zx::Duration,
}

impl Measurement {
    #[cfg(test)]
    pub fn empty(timestamp: zx::Time) -> Self {
        Self {
            timestamp,
            cpu_time: zx::Duration::from_nanos(0),
            queue_time: zx::Duration::from_nanos(0),
        }
    }

    pub fn clone_with_time(m: &Self, timestamp: zx::Time) -> Self {
        Self { timestamp, cpu_time: *m.cpu_time(), queue_time: *m.queue_time() }
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

    fn can_merge(&self, other: &Self) -> bool {
        u128::from(self.timestamp().into_nanos().abs_diff(other.timestamp().into_nanos()))
            <= MEASUREMENT_EPSILON.as_nanos()
    }
}

impl AddAssign<&Measurement> for Measurement {
    fn add_assign(&mut self, other: &Measurement) {
        self.cpu_time += other.cpu_time;
        self.queue_time += other.queue_time;
    }
}

impl SubAssign<&Measurement> for Measurement {
    fn sub_assign(&mut self, other: &Measurement) {
        self.cpu_time -= other.cpu_time;
        self.queue_time -= other.queue_time;
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
enum MostRecentMeasurement {
    Init,
    Measurement(Measurement),
    PostInvalidationMeasurement,
}

impl MostRecentMeasurement {
    fn update(&mut self, incoming: Option<Measurement>) {
        let this = std::mem::replace(self, Self::Init);
        *self = match (this, incoming) {
            (Self::Init, Some(m)) => Self::Measurement(m),
            (_, None) => Self::PostInvalidationMeasurement,
            (Self::Measurement(m1), Some(m2)) => Self::Measurement(max(m1, m2)),
            (Self::PostInvalidationMeasurement, _) => Self::PostInvalidationMeasurement,
        }
    }

    fn combine(&mut self, incoming: Self) {
        let this = std::mem::replace(self, Self::Init);
        *self = match (this, incoming) {
            (Self::Init, other)
            | (Self::PostInvalidationMeasurement, other)
            | (other, Self::PostInvalidationMeasurement)
            | (other, Self::Init) => other,
            (Self::Measurement(m1), Self::Measurement(m2)) => Self::Measurement(max(m1, m2)),
        }
    }
}

/// MeasurementsQueue is a priority queue with a maximum size. It guarantees that there will be
/// at most `max_measurements` true measurements and post invalidation measurements.
///
/// A "true" measurement is an instance of `Measurement`. A post invalidation measurement is a
/// counter incremented by `MeasurementsQueue::post_invalidation_insertion`, tracking how many
/// measurements would have been taken if the owning task wasn't invalid. The goal is to keep
/// a record of measurements for `max_measurements` minutes.
///
/// The queue is prioritized by `Measurement`'s `Ord` impl such that the oldest
/// measurements are dropped first when `max_period` is exceeded. No two measurements should have
/// the same timestamp.
#[derive(Debug)]
pub struct MeasurementsQueue {
    values: BinaryHeap<Reverse<Measurement>>,
    // outer option refers to initialization
    most_recent_measurement: MostRecentMeasurement,
    ts: Arc<dyn TimeSource + Send + Sync>,
    max_period: zx::Duration,
    max_measurements: usize,
}

/// Merge two queues together.
///
/// `AddAssign` sets `self.post_invalidation_measurements` to the minimum
/// of the values of the two queues.
impl AddAssign<Self> for MeasurementsQueue {
    fn add_assign(&mut self, other: Self) {
        // collect the measurements into an owning vector, arbitrarily ordered
        let mut rhs_values = other.values.into_vec();
        let mut new_heap = BinaryHeap::new();

        while let Some(Reverse(mut lhs)) = self.values.pop() {
            rhs_values = rhs_values
                .into_iter()
                .filter_map(|Reverse(rhs)| {
                    if lhs.can_merge(&rhs) {
                        lhs += &rhs;
                        None
                    } else {
                        Some(Reverse(rhs))
                    }
                })
                .collect();

            new_heap.push(Reverse(lhs));
        }

        for leftover in rhs_values {
            new_heap.push(leftover);
        }

        self.values = new_heap;
        self.most_recent_measurement.combine(other.most_recent_measurement);
        self.clean_stale();
    }
}

impl MeasurementsQueue {
    pub fn new(max_measurements: usize, ts: Arc<dyn TimeSource + Send + Sync>) -> Self {
        Self {
            values: BinaryHeap::new(),
            most_recent_measurement: MostRecentMeasurement::Init,
            ts,
            max_period: (CPU_SAMPLE_PERIOD * max_measurements as u32).into(),
            max_measurements,
        }
    }

    /// Insert a new measurement into the priority queue.
    /// Measurements must have distinct timestamps.
    pub fn insert(&mut self, measurement: Measurement) {
        self.insert_internal(Some(measurement));
    }

    /// Insert a false measurement, typically after the invalidation of a task.
    pub fn insert_post_invalidation(&mut self) {
        self.insert_internal(None);
    }

    fn insert_internal(&mut self, measurement_wrapper: Option<Measurement>) {
        self.most_recent_measurement.update(measurement_wrapper.clone());

        if let Some(measurement) = measurement_wrapper {
            self.values.push(Reverse(measurement));
        }

        self.clean_stale();
    }

    fn clean_stale(&mut self) {
        let now = zx::Time::from_nanos(self.ts.now());
        while let Some(Reverse(oldest)) = self.values.peek() {
            if (*oldest.timestamp() > now - self.max_period)
                && self.values.len() <= self.max_measurements
            {
                return;
            }

            self.values.pop();
        }
    }

    #[cfg(test)]
    pub fn true_measurement_count(&self) -> usize {
        self.values.len()
    }

    /// Sorted from newest to oldest:
    /// Index: Timestamp
    /// 0: t + N
    /// 1: t+ (N-1)
    /// 2: t+ (N-2)
    /// ...
    /// N: t
    pub fn iter_sorted(&self) -> impl DoubleEndedIterator<Item = Measurement> {
        self.values.clone().into_sorted_vec().into_iter().map(|Reverse(v)| v).into_iter()
    }

    /// Checks whether or not there are any true measurements. This says nothing
    /// about the number of post invalidation measurements.
    pub fn no_true_measurements(&self) -> bool {
        self.values.is_empty()
    }

    /// Access the youngest true Measurement in the queue.
    /// Returns `None` if there are `post_invalidation_measurements`, or if there
    /// are no true measurements.
    pub fn most_recent_measurement(&self) -> Option<&'_ Measurement> {
        match self.most_recent_measurement {
            MostRecentMeasurement::Init | MostRecentMeasurement::PostInvalidationMeasurement => {
                None
            }
            MostRecentMeasurement::Measurement(ref v) => Some(v),
        }
    }

    pub fn record_to_node(&self, parent: &inspect::Node) {
        let samples = parent.create_child(&*SAMPLES);
        // gather measurements ordered oldest -> newest
        for (i, measurement) in self.iter_sorted().rev().enumerate() {
            let child = samples.create_child(&SAMPLE_INDEXES[i]);
            measurement.record_to_node(&child);
            samples.record(child);
        }
        parent.record(samples);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::diagnostics::cpu::constants::COMPONENT_CPU_MAX_SAMPLES;
    use fuchsia_zircon::{Duration as ZxDuration, Time};
    use injectable_time::{FakeTime, TimeSource};
    use std::time::Duration;

    fn insert_default(q: &mut MeasurementsQueue, clock: &FakeTime) {
        q.insert(Measurement::empty(Time::from_nanos(clock.now())));
        clock.add_ticks(CPU_SAMPLE_PERIOD.as_nanos() as i64);
    }

    fn insert_measurement(q: &mut MeasurementsQueue, clock: &FakeTime, value: Measurement) {
        q.insert(value);
        clock.add_ticks(CPU_SAMPLE_PERIOD.as_nanos() as i64);
    }

    #[fuchsia::test]
    fn insert_to_measurements_queue() {
        let clock = FakeTime::new();
        let mut q = MeasurementsQueue::new(COMPONENT_CPU_MAX_SAMPLES, Arc::new(clock.clone()));
        q.insert(Measurement::empty(Time::from_nanos(clock.now())));
        clock.add_ticks(CPU_SAMPLE_PERIOD.as_nanos() as i64);

        assert_eq!(1, q.true_measurement_count());

        for _ in 0..COMPONENT_CPU_MAX_SAMPLES * 2 {
            q.insert(Measurement::empty(Time::from_nanos(clock.now())));
            clock.add_ticks(CPU_SAMPLE_PERIOD.as_nanos() as i64);
        }

        assert_eq!(COMPONENT_CPU_MAX_SAMPLES, q.true_measurement_count());
    }

    #[fuchsia::test]
    fn test_back() {
        let clock = FakeTime::new();
        let mut q = MeasurementsQueue::new(COMPONENT_CPU_MAX_SAMPLES, Arc::new(clock.clone()));

        insert_default(&mut q, &clock);
        insert_default(&mut q, &clock);
        insert_default(&mut q, &clock);
        insert_default(&mut q, &clock);

        let now = clock.now();
        insert_default(&mut q, &clock);

        assert_eq!(now, q.most_recent_measurement().unwrap().timestamp().into_nanos());

        q.insert_post_invalidation();

        assert!(q.most_recent_measurement().is_none());
    }

    #[fuchsia::test]
    fn post_invalidation_pushes_true_measurements_out() {
        let clock = FakeTime::new();
        let mut q = MeasurementsQueue::new(COMPONENT_CPU_MAX_SAMPLES, Arc::new(clock.clone()));

        assert!(q.no_true_measurements());
        assert!(q.most_recent_measurement().is_none());
        assert_eq!(0, q.true_measurement_count());

        for _ in 0..COMPONENT_CPU_MAX_SAMPLES / 2 {
            insert_default(&mut q, &clock);
        }

        assert!(!q.no_true_measurements());
        assert!(q.most_recent_measurement().is_some());
        assert_eq!(COMPONENT_CPU_MAX_SAMPLES / 2, q.true_measurement_count());

        for _ in 0..COMPONENT_CPU_MAX_SAMPLES / 2 {
            q.insert_post_invalidation();
            clock.add_ticks(CPU_SAMPLE_PERIOD.as_nanos() as i64);
        }

        assert!(!q.no_true_measurements());
        assert!(q.most_recent_measurement().is_none());
        assert_eq!(COMPONENT_CPU_MAX_SAMPLES / 2, q.true_measurement_count());

        for _ in 0..COMPONENT_CPU_MAX_SAMPLES {
            q.insert_post_invalidation();
            clock.add_ticks(CPU_SAMPLE_PERIOD.as_nanos() as i64);
        }

        assert!(q.no_true_measurements());
        assert!(q.most_recent_measurement().is_none());
        assert_eq!(0, q.true_measurement_count());
    }

    #[fuchsia::test]
    fn add_assign() {
        // the two queues are shifted apart by two seconds
        let clock1 = FakeTime::new();
        let clock2 = FakeTime::new();
        clock2.set_ticks(Duration::from_secs(2).as_nanos() as i64);

        let mut q1 = MeasurementsQueue::new(COMPONENT_CPU_MAX_SAMPLES, Arc::new(clock1.clone()));
        let mut q2 = MeasurementsQueue::new(COMPONENT_CPU_MAX_SAMPLES, Arc::new(clock2.clone()));

        let mut m1 = Measurement::empty(Time::from_nanos(clock1.now()));
        let mut m2 = Measurement::empty(Time::from_nanos(clock2.now()));
        m1.cpu_time = Duration::from_secs(1).into();
        m2.cpu_time = Duration::from_secs(3).into();
        insert_measurement(&mut q1, &clock1, m1);
        insert_measurement(&mut q2, &clock2, m2);

        for _ in 0..COMPONENT_CPU_MAX_SAMPLES {
            let mut m1 = Measurement::empty(Time::from_nanos(clock1.now()));
            let mut m2 = Measurement::empty(Time::from_nanos(clock2.now()));
            m1.cpu_time = Duration::from_secs(1).into();
            m2.cpu_time = Duration::from_secs(3).into();
            insert_measurement(&mut q1, &clock1, m1);
            insert_measurement(&mut q2, &clock2, m2);
        }

        q1 += q2;

        let expected: ZxDuration = Duration::from_secs(4).into();
        for m in q1.iter_sorted() {
            assert_eq!(&expected, m.cpu_time());
        }
    }

    #[fuchsia::test]
    fn add_assign_missing_matches() {
        let clock1 = FakeTime::new();
        let clock2 = FakeTime::new();
        clock2.set_ticks(Duration::from_secs(125).as_nanos() as i64);

        let max_values = 5;

        let mut q1 = MeasurementsQueue::new(max_values, Arc::new(clock1.clone()));
        let mut q2 = MeasurementsQueue::new(max_values, Arc::new(clock2.clone()));

        let mut m1 = Measurement::empty(Time::from_nanos(clock1.now()));
        let mut m2 = Measurement::empty(Time::from_nanos(clock2.now()));
        m1.cpu_time = Duration::from_secs(1).into();
        m2.cpu_time = Duration::from_secs(3).into();
        insert_measurement(&mut q1, &clock1, m1);
        insert_measurement(&mut q2, &clock2, m2);

        for _ in 1..max_values {
            let mut m1 = Measurement::empty(Time::from_nanos(clock1.now()));
            let mut m2 = Measurement::empty(Time::from_nanos(clock2.now()));
            m1.cpu_time = Duration::from_secs(1).into();
            m2.cpu_time = Duration::from_secs(3).into();
            insert_measurement(&mut q1, &clock1, m1);
            insert_measurement(&mut q2, &clock2, m2);
        }

        // t   q1  q2
        // -------------
        // 0   1
        // -------------
        // 60  1
        // -------------
        // 120 1
        // 125     3
        // -------------
        // 180 1
        // 185     3
        // -------------
        // 240 1
        // 245     3
        // -------------
        // 300
        // 305     3
        // -------------
        // 360
        // 365     3
        // -------------
        // 420
        // 425

        // the merged clock needs to have the largest time; in this case,
        // it's known that queue_clock2 is "more recent"
        clock1.set_ticks(clock2.now());
        q1 += q2;

        let sorted = q1.values.into_sorted_vec();
        let actual = sorted.iter().map(|Reverse(m)| m).collect::<Vec<_>>();

        let d = |secs| -> ZxDuration { Duration::from_secs(secs).into() };
        assert_eq!(&d(3), actual[0].cpu_time());
        assert_eq!(&d(3), actual[1].cpu_time());
        assert_eq!(&d(4), actual[2].cpu_time());
        assert_eq!(&d(4), actual[3].cpu_time());
        assert_eq!(max_values - 1, actual.len());
    }

    #[fuchsia::test]
    fn add_assign_post_invalidation() {
        let clock1 = FakeTime::new();
        let clock2 = FakeTime::new();
        clock2.set_ticks(Duration::from_secs(125).as_nanos() as i64);

        let max_values = 5;

        let mut q1 = MeasurementsQueue::new(max_values, Arc::new(clock1.clone()));
        let mut q2 = MeasurementsQueue::new(max_values, Arc::new(clock2.clone()));

        for _ in 0..max_values {
            let mut m1 = Measurement::empty(Time::from_nanos(clock1.now()));
            let mut m2 = Measurement::empty(Time::from_nanos(clock2.now()));
            m1.cpu_time = Duration::from_secs(1).into();
            m2.cpu_time = Duration::from_secs(3).into();
            insert_measurement(&mut q1, &clock1, m1);
            insert_measurement(&mut q2, &clock2, m2);
        }

        q1.insert_post_invalidation();
        q2.insert_post_invalidation();

        clock1.add_ticks(CPU_SAMPLE_PERIOD.as_nanos() as i64);
        q1.insert_post_invalidation();

        clock1.add_ticks(CPU_SAMPLE_PERIOD.as_nanos() as i64);
        q1.insert_post_invalidation();

        // t   q1  q2
        // -------------
        // 60  1
        // -------------
        // 120 1
        // -------------
        // 180 1
        // 185     3
        // -------------
        // 240 1
        // 245     3
        // -------------
        // 300 1
        // 305     3
        // -------------
        // 360 p
        // 365     3
        // -------------
        // 420 p
        // 425     3
        // -------------
        // 480 p
        // 485     p
        // -------------

        q1 += q2;

        let sorted = q1.values.into_sorted_vec();
        let actual = sorted.into_iter().map(|Reverse(m)| m).collect::<Vec<_>>();

        let d = |secs| -> ZxDuration { Duration::from_secs(secs).into() };
        assert_eq!(&d(3), actual[0].cpu_time());
        assert_eq!(&d(3), actual[1].cpu_time());
        assert_eq!(&d(4), actual[2].cpu_time());
        assert_eq!(&d(4), actual[2].cpu_time());
        assert_eq!(4, actual.len());
    }

    #[fuchsia::test]
    fn size_limited_to_max_no_matter_duration() {
        let max_values = 20;
        let mut q = MeasurementsQueue::new(max_values, Arc::new(FakeTime::new()));

        for _ in 0..(max_values + 100) {
            q.insert(Measurement::empty(Time::get_monotonic()));
        }

        assert_eq!(max_values, q.true_measurement_count());
    }
}
