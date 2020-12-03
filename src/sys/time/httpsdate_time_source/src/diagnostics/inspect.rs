// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::SAMPLE_POLLS;
use crate::datatypes::{HttpsSample, Phase};
use crate::diagnostics::Diagnostics;
use fuchsia_inspect::{
    ArrayProperty, IntArrayProperty, IntProperty, Node, NumericProperty, Property, StringProperty,
    UintProperty,
};
use fuchsia_zircon as zx;
use httpdate_hyper::HttpsDateError;
use log::warn;
use parking_lot::Mutex;
use std::collections::HashMap;

/// Maximum number of successful samples recorded.
const SAMPLES_RECORDED: usize = 5;
/// Empty sample with which the sample buffer is originally initialized.
const EMPTY_SAMPLE: HttpsSample = HttpsSample {
    utc: zx::Time::ZERO,
    monotonic: zx::Time::ZERO,
    standard_deviation: zx::Duration::from_nanos(0),
    final_bound_size: zx::Duration::from_nanos(0),
    round_trip_times: vec![],
};

/// Struct containing inspect metrics for HTTPSDate.
pub struct InspectDiagnostics {
    /// Node holding failure counts.
    failure_node: Node,
    /// Monotonic time at which the last error occurred.
    last_failure_time: IntProperty,
    /// Counters for failed attempts to produce samples, keyed by error type.
    failure_counts: Mutex<HashMap<HttpsDateError, UintProperty>>,
    /// Diagnostic data for the most recent successful samples.
    recent_successes_buffer: Mutex<SampleMetricBuffer>,
    /// The current phase the algorithm is in.
    phase: StringProperty,
    /// Monotonic time at which the phase was last updated.
    phase_update_time: IntProperty,
}

impl InspectDiagnostics {
    /// Create a new `InspectDiagnostics` that records diagnostics to the provided root node.
    pub fn new(root_node: &Node) -> Self {
        InspectDiagnostics {
            failure_node: root_node.create_child("failure_counts"),
            last_failure_time: root_node.create_int("last_failure_time", 0),
            failure_counts: Mutex::new(HashMap::new()),
            recent_successes_buffer: Mutex::new(SampleMetricBuffer::new(
                root_node,
                SAMPLES_RECORDED,
            )),
            phase: root_node.create_string("phase", &format!("{:?}", Phase::Initial)),
            phase_update_time: root_node
                .create_int("phase_update_time", zx::Time::get_monotonic().into_nanos()),
        }
    }
}

impl Diagnostics for InspectDiagnostics {
    fn success(&self, sample: &HttpsSample) {
        self.recent_successes_buffer.lock().update(sample);
    }

    fn failure(&self, error: &HttpsDateError) {
        let mut failure_counts_lock = self.failure_counts.lock();
        match failure_counts_lock.get(error) {
            Some(uint_property) => uint_property.add(1),
            None => {
                failure_counts_lock
                    .insert(*error, self.failure_node.create_uint(format!("{:?}", error), 1));
            }
        }
        self.last_failure_time.set(zx::Time::get_monotonic().into_nanos());
    }

    fn phase_update(&self, phase: &Phase) {
        self.phase.set(&format!("{:?}", phase));
        self.phase_update_time.set(zx::Time::get_monotonic().into_nanos());
    }
}

/// A circular buffer for inspect that records the last `count` samples.
struct SampleMetricBuffer {
    /// Number of samples processed.
    count: usize,
    /// Nodes containing sample diagnostic data.
    sample_metrics: Vec<SampleMetric>,
    /// Counters indicating the order of the samples.
    counters: Vec<UintProperty>,
}

impl SampleMetricBuffer {
    /// Create a new buffer containing `size` entries at `root_node`.
    fn new(root_node: &Node, size: usize) -> Self {
        let mut sample_metrics = Vec::with_capacity(size);
        let mut counters = Vec::with_capacity(size);
        for i in 0..size {
            let node = root_node.create_child(format!("sample_{}", i));
            counters.push(node.create_uint("counter", 0));
            sample_metrics.push(SampleMetric::new(node, &EMPTY_SAMPLE));
        }
        Self { count: 0, sample_metrics, counters }
    }

    /// Add a new sample to the buffer, evicting the oldest if necessary.
    fn update(&mut self, sample: &HttpsSample) {
        let index = self.count % self.sample_metrics.len();
        self.count += 1;
        self.sample_metrics[index].update(sample);
        self.counters[index].set(self.count as u64);
    }
}

/// Diagnostic metrics for a sample.
// TODO(satsukiu): add support for array properties in derive InspectWritable
struct SampleMetric {
    /// Node containing the sample metrics.
    _node: Node,
    /// Array of measured network round trip times for the sample in nanoseconds.
    round_trip_times: IntArrayProperty,
    /// Monotonic time at which the sample was taken.
    monotonic: IntProperty,
    /// Final size of the produced UTC bound in nanoseconds.
    bound_size: IntProperty,
}

impl SampleMetric {
    /// Create a new `SampleMetric` that records to the given Node.
    fn new(node: Node, sample: &HttpsSample) -> Self {
        let round_trip_times = node.create_int_array("round_trip_times", SAMPLE_POLLS);
        if sample.round_trip_times.len() > SAMPLE_POLLS {
            warn!(
                "Truncating {:?} round trip time entries to {:?} to fit in inspect",
                sample.round_trip_times.len(),
                SAMPLE_POLLS
            );
        }
        sample
            .round_trip_times
            .iter()
            .enumerate()
            .take(SAMPLE_POLLS)
            .for_each(|(idx, duration)| round_trip_times.set(idx, duration.into_nanos()));
        let monotonic = node.create_int("monotonic", sample.monotonic.into_nanos());
        let bound_size = node.create_int("bound_size", sample.final_bound_size.into_nanos());
        Self { _node: node, round_trip_times, monotonic, bound_size }
    }

    /// Update the recorded values in the inspect Node.
    fn update(&self, sample: &HttpsSample) {
        if sample.round_trip_times.len() > SAMPLE_POLLS {
            warn!(
                "Truncating {:?} round trip time entries to {:?} to fit in inspect",
                sample.round_trip_times.len(),
                SAMPLE_POLLS
            );
        }
        self.round_trip_times.clear();
        sample
            .round_trip_times
            .iter()
            .enumerate()
            .take(SAMPLE_POLLS)
            .for_each(|(idx, duration)| self.round_trip_times.set(idx, duration.into_nanos()));
        self.bound_size.set(sample.final_bound_size.into_nanos());
        self.monotonic.set(sample.monotonic.into_nanos());
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty, Inspector};
    use fuchsia_zircon as zx;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref TEST_UTC: zx::Time = zx::Time::from_nanos(999_900_000_000);
        static ref TEST_MONOTONIC: zx::Time = zx::Time::from_nanos(550_000_000_000);
    }

    const TEST_STANDARD_DEVIATION: zx::Duration = zx::Duration::from_millis(211);

    const TEST_ROUND_TRIP_1: zx::Duration = zx::Duration::from_millis(100);
    const TEST_ROUND_TRIP_2: zx::Duration = zx::Duration::from_millis(150);
    const TEST_ROUND_TRIP_3: zx::Duration = zx::Duration::from_millis(200);

    const TEST_BOUND_SIZE: zx::Duration = zx::Duration::from_millis(75);

    fn sample_with_rtts(round_trip_times: &[zx::Duration]) -> HttpsSample {
        HttpsSample {
            utc: *TEST_UTC,
            monotonic: *TEST_MONOTONIC,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            round_trip_times: round_trip_times.to_vec(),
        }
    }

    #[test]
    fn test_successes() {
        let inspector = Inspector::new();
        let inspect = InspectDiagnostics::new(inspector.root());
        assert_inspect_tree!(
            inspector,
            root: contains {
                failure_counts: {}
            }
        );

        inspect.success(&sample_with_rtts(&[TEST_ROUND_TRIP_1, TEST_ROUND_TRIP_2]));
        inspect.success(&sample_with_rtts(&[
            TEST_ROUND_TRIP_1,
            TEST_ROUND_TRIP_2,
            TEST_ROUND_TRIP_3,
        ]));
        assert_inspect_tree!(
            inspector,
            root: contains {
                sample_0: {
                    counter: 1u64,
                    round_trip_times: vec![
                        TEST_ROUND_TRIP_1.into_nanos(),
                        TEST_ROUND_TRIP_2.into_nanos(),
                        0,
                        0,
                        0
                    ],
                    monotonic: TEST_MONOTONIC.into_nanos(),
                    bound_size: TEST_BOUND_SIZE.into_nanos(),
                },
                sample_1: {
                    counter: 2u64,
                    round_trip_times: vec![
                        TEST_ROUND_TRIP_1.into_nanos(),
                        TEST_ROUND_TRIP_2.into_nanos(),
                        TEST_ROUND_TRIP_3.into_nanos(),
                        0,
                        0
                    ],
                    monotonic: TEST_MONOTONIC.into_nanos(),
                    bound_size: TEST_BOUND_SIZE.into_nanos(),
                }
            }
        );
    }

    #[test]
    fn test_success_overwrite_on_overflow() {
        let inspector = Inspector::new();
        let inspect = InspectDiagnostics::new(inspector.root());
        assert_inspect_tree!(
            inspector,
            root: contains {
                failure_counts: {}
            }
        );

        for _ in 0..SAMPLES_RECORDED {
            inspect.success(&sample_with_rtts(&[
                TEST_ROUND_TRIP_1,
                TEST_ROUND_TRIP_2,
                TEST_ROUND_TRIP_3,
                TEST_ROUND_TRIP_1,
                TEST_ROUND_TRIP_2,
            ]));
        }

        // Recording a new success should wrap the buffer and zero out unused rtt entries.
        inspect.success(&sample_with_rtts(&[TEST_ROUND_TRIP_2]));
        assert_inspect_tree!(
            inspector,
            root: contains {
                sample_0: contains {
                    counter: 6u64,
                    round_trip_times: vec![
                        TEST_ROUND_TRIP_2.into_nanos(),
                        0,
                        0,
                        0,
                        0,
                    ],
                },
                sample_1: contains {
                    counter: 2u64,
                    round_trip_times: vec![
                        TEST_ROUND_TRIP_1.into_nanos(),
                        TEST_ROUND_TRIP_2.into_nanos(),
                        TEST_ROUND_TRIP_3.into_nanos(),
                        TEST_ROUND_TRIP_1.into_nanos(),
                        TEST_ROUND_TRIP_2.into_nanos(),
                    ]
                }
            }
        );
    }

    #[test]
    fn test_failure() {
        let inspector = Inspector::new();
        let inspect = InspectDiagnostics::new(inspector.root());
        assert_inspect_tree!(
            inspector,
            root: contains {
                failure_counts: {},
                last_failure_time: 0i64,
            }
        );

        inspect.failure(&HttpsDateError::NoCertificatesPresented);
        assert_inspect_tree!(
            inspector,
            root: contains {
                failure_counts: {
                    NoCertificatesPresented: 1u64,
                },
                last_failure_time: AnyProperty,
            }
        );

        inspect.failure(&HttpsDateError::NoCertificatesPresented);
        inspect.failure(&HttpsDateError::NetworkError);
        assert_inspect_tree!(
            inspector,
            root: contains {
                failure_counts: {
                    NoCertificatesPresented: 2u64,
                    NetworkError: 1u64,
                },
                last_failure_time: AnyProperty,
            }
        );
    }

    #[test]
    fn test_phase() {
        let inspector = Inspector::new();
        let inspect = InspectDiagnostics::new(inspector.root());
        assert_inspect_tree!(
            inspector,
            root: contains {
                phase: "Initial",
                phase_update_time: AnyProperty,
            }
        );
        inspect.phase_update(&Phase::Maintain);
        assert_inspect_tree!(
            inspector,
            root: contains {
                phase: "Maintain",
                phase_update_time: AnyProperty,
            }
        );
    }
}
