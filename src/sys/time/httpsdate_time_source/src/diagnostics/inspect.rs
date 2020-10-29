// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

// TODO(satsukiu): once the new algorithm is in place this constant should live
// elsewhere.
const MAX_POLLS_PER_SAMPLE: usize = 5;

/// Struct containing inspect metrics for HTTPSDate.
pub struct InspectDiagnostics {
    /// Top level Node for diagnostics.
    root_node: Node,
    /// Counter for samples successfully produced.
    success_count: UintProperty,
    /// Node holding failure counts.
    failure_node: Node,
    /// Counters for failed attempts to produce samples, keyed by error type.
    failure_counts: Mutex<HashMap<HttpsDateError, UintProperty>>,
    /// Diagnostic data for the last successful sample.
    last_successful: Mutex<Option<SampleMetric>>,
    /// The current phase the algorithm is in.
    phase: StringProperty,
    /// Monotonic time at which the phase was last updated.
    phase_update_time: IntProperty,
}

impl InspectDiagnostics {
    /// Create a new `InspectDiagnostics` that records diagnostics to the provided root node.
    pub fn new(root_node: &Node) -> Self {
        InspectDiagnostics {
            root_node: root_node.clone_weak(),
            success_count: root_node.create_uint("success_count", 0),
            failure_node: root_node.create_child("failure_counts"),
            failure_counts: Mutex::new(HashMap::new()),
            last_successful: Mutex::new(None),
            phase: root_node.create_string("phase", &format!("{:?}", Phase::Initial)),
            phase_update_time: root_node
                .create_int("phase_update_time", zx::Time::get_monotonic().into_nanos()),
        }
    }
}

impl Diagnostics for InspectDiagnostics {
    fn success(&self, sample: &HttpsSample) {
        self.success_count.add(1);
        let mut last_successful_lock = self.last_successful.lock();
        match &*last_successful_lock {
            Some(last) => last.update(sample),
            None => {
                last_successful_lock.replace(SampleMetric::new(
                    self.root_node.create_child("last_successful"),
                    sample,
                ));
            }
        }
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
    }

    fn phase_update(&self, phase: &Phase) {
        self.phase.set(&format!("{:?}", phase));
        self.phase_update_time.set(zx::Time::get_monotonic().into_nanos());
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
        let round_trip_times = node.create_int_array("round_trip_times", MAX_POLLS_PER_SAMPLE);
        if sample.round_trip_times.len() > MAX_POLLS_PER_SAMPLE {
            warn!(
                "Truncating {:?} round trip time entries to {:?} to fit in inspect",
                sample.round_trip_times.len(),
                MAX_POLLS_PER_SAMPLE
            );
        }
        sample
            .round_trip_times
            .iter()
            .enumerate()
            .take(MAX_POLLS_PER_SAMPLE)
            .for_each(|(idx, duration)| round_trip_times.set(idx, duration.into_nanos()));
        let monotonic = node.create_int("monotonic", sample.monotonic.into_nanos());
        let bound_size = node.create_int("bound_size", sample.final_bound_size.into_nanos());
        Self { _node: node, round_trip_times, monotonic, bound_size }
    }

    /// Update the recorded values in the inspect Node.
    fn update(&self, sample: &HttpsSample) {
        if sample.round_trip_times.len() > MAX_POLLS_PER_SAMPLE {
            warn!(
                "Truncating {:?} round trip time entries to {:?} to fit in inspect",
                sample.round_trip_times.len(),
                MAX_POLLS_PER_SAMPLE
            );
        }
        self.round_trip_times.clear();
        sample
            .round_trip_times
            .iter()
            .enumerate()
            .take(MAX_POLLS_PER_SAMPLE)
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
    fn test_success() {
        let inspector = Inspector::new();
        let inspect = InspectDiagnostics::new(inspector.root());
        assert_inspect_tree!(
            inspector,
            root: contains {
                success_count: 0u64,
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
                success_count: 2u64,
                last_successful: {
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
    fn test_success_overwrite_round_trip_times() {
        let inspector = Inspector::new();
        let inspect = InspectDiagnostics::new(inspector.root());
        assert_inspect_tree!(
            inspector,
            root: contains {
                success_count: 0u64,
                failure_counts: {}
            }
        );

        inspect.success(&sample_with_rtts(&[
            TEST_ROUND_TRIP_1,
            TEST_ROUND_TRIP_2,
            TEST_ROUND_TRIP_3,
            TEST_ROUND_TRIP_1,
            TEST_ROUND_TRIP_2,
        ]));

        // Recording a new success with less round trip times should zero out the unused entries.
        inspect.success(&sample_with_rtts(&[TEST_ROUND_TRIP_2]));
        assert_inspect_tree!(
            inspector,
            root: contains {
                last_successful: contains {
                    round_trip_times: vec![
                        TEST_ROUND_TRIP_2.into_nanos(),
                        0,
                        0,
                        0,
                        0,
                    ],
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
                success_count: 0u64,
                failure_counts: {}
            }
        );

        inspect.failure(&HttpsDateError::NoCertificatesPresented);
        assert_inspect_tree!(
            inspector,
            root: contains {
                failure_counts: {
                    NoCertificatesPresented: 1u64,
                },
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
