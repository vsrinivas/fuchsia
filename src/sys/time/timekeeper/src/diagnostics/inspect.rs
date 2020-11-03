// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{Diagnostics, Event},
        enums::{
            ClockCorrectionStrategy, ClockUpdateReason, InitializeRtcOutcome, Role,
            SampleValidationError, TimeSourceError, Track, WriteRtcOutcome,
        },
        MonitorTrack, PrimaryTrack, TimeSource,
    },
    fidl_fuchsia_time_external::Status,
    fuchsia_inspect::{
        health::Reporter, Inspector, IntProperty, Node, NumericProperty, Property, StringProperty,
        UintProperty,
    },
    fuchsia_zircon::{self as zx, HandleBased as _},
    futures::FutureExt,
    inspect_writable::{InspectWritable, InspectWritableNode},
    lazy_static::lazy_static,
    log::warn,
    parking_lot::Mutex,
    std::{collections::HashMap, sync::Arc},
};

const ONE_MILLION: u32 = 1_000_000;
/// The value stored in place of any time that could not be generated.
const FAILED_TIME: i64 = -1;
/// The number of time estimates that are retained.
const ESTIMATE_UPDATE_COUNT: usize = 5;
/// The number of clock corrections that are retained.
const CLOCK_CORRECTION_COUNT: usize = 3;

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
}

fn monotonic_time() -> i64 {
    zx::Time::get_monotonic().into_nanos()
}

fn utc_time() -> i64 {
    zx::Time::get(zx::ClockId::UTC).into_nanos()
}

/// A vector of inspect nodes used to store some struct implementing `InspectWritable`, where the
/// contents of the oldest node are replaced on each write.
///
/// An 'counter' field is added to each node labeling which write led to the current node contents.
/// The first write will have a counter of 1 and the node with the highest counter value is always
/// the most recently written.
///
/// Potentially this is worth moving into a library at some point.
pub struct CircularBuffer<T: InspectWritable + Default> {
    count: usize,
    nodes: Vec<T::NodeType>,
    counters: Vec<UintProperty>,
}

impl<T: InspectWritable + Default> CircularBuffer<T> {
    /// Construct a new `CircularBuffer` of the supplied size within the supplied parent node.
    /// Each node is named with the supplied prefix and an integer suffix, all nodes are initialized
    /// to default values.
    fn new(size: usize, prefix: &str, node: &Node) -> Self {
        let mut nodes: Vec<T::NodeType> = Vec::new();
        let mut counters: Vec<UintProperty> = Vec::new();
        for i in 0..size {
            let child = node.create_child(format!("{}{}", prefix, i));
            counters.push(child.create_uint("counter", 0));
            nodes.push(T::default().create(child));
        }
        CircularBuffer { count: 0, nodes, counters }
    }

    /// Write the supplied data into the oldest node in the circular buffer.
    fn update(&mut self, data: &T) {
        let index = self.count % self.nodes.len();
        self.count += 1;
        self.nodes[index].update(data);
        self.counters[index].set(self.count as u64);
    }
}

/// A representation of a point in time as measured by all pertinent clocks.
#[derive(InspectWritable)]
pub struct TimeSet {
    /// The kernel ZX_CLOCK_MONOTONIC time, in ns.
    monotonic: i64,
    /// The kernel ZX_CLOCK_UTC time, in ns.
    kernel_utc: i64,
    /// The UTC zx::Clock time, in ns.
    clock_utc: i64,
}

impl TimeSet {
    /// Creates a new `TimeSet` set to current time.
    pub fn now(clock: &zx::Clock) -> Self {
        TimeSet {
            monotonic: monotonic_time(),
            kernel_utc: utc_time(),
            clock_utc: clock.read().map(zx::Time::into_nanos).unwrap_or(FAILED_TIME),
        }
    }
}

/// A representation of a single update to a UTC zx::Clock.
#[derive(InspectWritable)]
pub struct ClockDetails {
    /// The monotonic time at which the details were retrieved. Note this is the time the Rust
    /// object was created, which may not exactly match the time its contents were supplied by
    /// the kernel.
    retrieval_monotonic: i64,
    /// The generation counter as documented in the zx::Clock.
    generation_counter: u32,
    /// The monotonic time from the monotonic-UTC correspondence pair, in ns.
    monotonic_offset: i64,
    /// The UTC time from the monotonic-UTC correspondence pair, in ns.
    utc_offset: i64,
    /// The ratio between UTC tick rate and monotonic tick rate in parts per million, where
    /// a value above one million means UTC is ticking faster than monotonic.
    rate_ppm: u32,
    /// The error bounds as documented in the zx::Clock.
    error_bounds: u64,
    /// The reason this clock update occurred, if known.
    reason: Option<ClockUpdateReason>,
}

impl ClockDetails {
    /// Attaches a reason for the clock update.
    pub fn with_reason(mut self, reason: ClockUpdateReason) -> Self {
        self.reason = Some(reason);
        self
    }
}

impl From<zx::ClockDetails> for ClockDetails {
    fn from(details: zx::ClockDetails) -> ClockDetails {
        // Handle the potential for a divide by zero in an unset rate.
        let rate_ppm = match (
            details.mono_to_synthetic.rate.synthetic_ticks,
            details.mono_to_synthetic.rate.reference_ticks,
        ) {
            (0, _) => 0,
            (_, 0) => std::i64::MAX,
            (synthetic, reference) => (synthetic as i64 * ONE_MILLION as i64) / (reference as i64),
        };
        ClockDetails {
            retrieval_monotonic: monotonic_time(),
            generation_counter: details.generation_counter,
            monotonic_offset: details.mono_to_synthetic.reference_offset,
            utc_offset: details.mono_to_synthetic.synthetic_offset,
            rate_ppm: rate_ppm as u32,
            error_bounds: details.error_bounds,
            reason: None,
        }
    }
}

/// An inspect `Node` and properties used to describe interactions with a real time clock.
struct RealTimeClockNode {
    /// The number of successful writes to the RTC.
    write_success_counter: UintProperty,
    /// The number of failed writes to the RTC.
    write_failure_counter: UintProperty,
    /// The inspect Node these fields are exported to.
    _node: Node,
}

impl RealTimeClockNode {
    /// Constructs a new `RealTimeClockNode`, recording the initial state.
    pub fn new(node: Node, outcome: InitializeRtcOutcome, initial_time: Option<zx::Time>) -> Self {
        node.record_string("initialization", format!("{:?}", outcome));
        if let Some(time) = initial_time {
            node.record_int("initial_time", time.into_nanos());
        }
        RealTimeClockNode {
            write_success_counter: node.create_uint("write_successes", 0u64),
            write_failure_counter: node.create_uint("write_failures", 0u64),
            _node: node,
        }
    }

    /// Records an attempt to write to the clock.
    pub fn write(&mut self, outcome: WriteRtcOutcome) {
        match outcome {
            WriteRtcOutcome::Succeeded => self.write_success_counter.add(1),
            WriteRtcOutcome::Failed => self.write_failure_counter.add(1),
        }
    }
}

/// An inspect `Node` and properties used to describe the health of a time source.
struct TimeSourceNode {
    /// The most recent status of the time source.
    status: StringProperty,
    /// The monotonic time at which the time source last changed.
    status_change: IntProperty,
    /// The number of time source failures for each failure mode.
    failure_counters: HashMap<TimeSourceError, UintProperty>,
    /// The number of sample validation failutes for each rejection mode.
    rejection_counters: HashMap<SampleValidationError, UintProperty>,
    /// The inspect Node these fields are exported to.
    node: Node,
}

impl TimeSourceNode {
    /// Constructs a new `TimeSourceNode`, recording the initial state.
    pub fn new<T: TimeSource>(node: Node, time_source: &T) -> Self {
        node.record_string("component", format!("{:?}", time_source));
        TimeSourceNode {
            status: node.create_string("status", "Launched"),
            status_change: node.create_int("status_change_monotonic", monotonic_time()),
            failure_counters: HashMap::new(),
            rejection_counters: HashMap::new(),
            node: node,
        }
    }

    /// Records a change in status of the time source.
    pub fn status(&mut self, status: Status) {
        self.status.set(&format!("{:?}", &status));
        self.status_change.set(monotonic_time());
    }

    /// Records a failure of the time source.
    pub fn failure(&mut self, error: TimeSourceError) {
        self.status.set(&format!("Failed({:?})", error));
        self.status_change.set(monotonic_time());
        match self.failure_counters.get_mut(&error) {
            Some(field) => field.add(1),
            None => {
                let property = self.node.create_uint(&format!("failure_count_{:?}", &error), 1);
                self.failure_counters.insert(error, property);
            }
        }
    }

    /// Records a rejection of a sample produced by the time source.
    pub fn sample_rejection(&mut self, error: SampleValidationError) {
        match self.rejection_counters.get_mut(&error) {
            Some(field) => field.add(1),
            None => {
                let property = self.node.create_uint(&format!("rejection_count_{:?}", &error), 1);
                self.rejection_counters.insert(error, property);
            }
        }
    }
}

/// A representation of a single update to a UTC estimate.
#[derive(InspectWritable, Default)]
pub struct Estimate {
    /// The monotonic time at which the estimate was received.
    monotonic: i64,
    /// Estimated UTC at reference minus monotonic time at reference, in nanoseconds.
    offset: i64,
    /// The square root of element [0,0] of the covariance matrix, in nanoseconds.
    sqrt_covariance: u64,
}

/// A representation of a single planned clock correction.
#[derive(InspectWritable, Default)]
pub struct ClockCorrection {
    /// The monotonic time at which the clock correction was received.
    monotonic: i64,
    /// The change to be applied to the current clock value, in nanoseconds.
    correction: i64,
    /// The strategy that will be used to apply this correction.
    strategy: ClockCorrectionStrategy,
}

/// An inspect `Node` and properties used to describe the state and history of a time track.
struct TrackNode {
    /// A circular buffer of recent updates to the time estimate.
    estimates: CircularBuffer<Estimate>,
    /// A circular buffer of recently planned clock corrections.
    corrections: CircularBuffer<ClockCorrection>,
    /// The inspect `Node` these fields are exported to.
    _node: Node,
}

impl TrackNode {
    /// Constructs a new `TrackNode`.
    pub fn new(node: Node) -> Self {
        TrackNode {
            estimates: CircularBuffer::new(ESTIMATE_UPDATE_COUNT, "estimate_", &node),
            corrections: CircularBuffer::new(CLOCK_CORRECTION_COUNT, "clock_correction_", &node),
            _node: node,
        }
    }

    /// Records a new estimate of time for the track.
    pub fn update_estimate(&mut self, offset: zx::Duration, sqrt_covariance: zx::Duration) {
        let estimate = Estimate {
            monotonic: monotonic_time(),
            offset: offset.into_nanos(),
            sqrt_covariance: sqrt_covariance.into_nanos() as u64,
        };
        self.estimates.update(&estimate);
    }

    /// Records a new planned correction for the clock.
    pub fn clock_correction(
        &mut self,
        correction: zx::Duration,
        strategy: ClockCorrectionStrategy,
    ) {
        let clock_correction = ClockCorrection {
            monotonic: monotonic_time(),
            correction: correction.into_nanos(),
            strategy,
        };
        self.corrections.update(&clock_correction);
    }
}

/// The complete set of Timekeeper information exported through Inspect.
pub struct InspectDiagnostics {
    /// The monotonic time at which the network became available, in nanoseconds.
    network_available_monotonic: Mutex<Option<IntProperty>>,
    /// Details of the health of time sources.
    time_sources: Mutex<HashMap<Role, TimeSourceNode>>,
    /// Details of the current state and history of time tracks.
    tracks: Mutex<HashMap<Track, TrackNode>>,
    /// Details of interactions with the real time clock.
    rtc: Mutex<Option<RealTimeClockNode>>,
    /// The details of the most recent update to the UTC zx::Clock.
    // TODO(jsankey): Consider moving this into the TrackNode.
    last_update: Mutex<Option<<ClockDetails as InspectWritable>::NodeType>>,
    /// The UTC clock that provides the `clock_utc` component of `TimeSet` data.
    // TODO(jsankey): Consider moving this into the TrackNode.
    clock: Arc<zx::Clock>,
    /// The inspect node used to export the contents of this `InspectDiagnostics`.
    node: Node,
    /// The inspect health node to expose component status.
    health: Mutex<fuchsia_inspect::health::Node>,
}

impl InspectDiagnostics {
    /// Construct a new `InspectDiagnostics` exporting at the supplied `Node` using data from
    /// the supplied clock.
    pub(crate) fn new<T: TimeSource>(
        node: &Node,
        primary: &PrimaryTrack<T>,
        optional_monitor: &Option<MonitorTrack<T>>,
    ) -> Self {
        // Arc-wrapping the clock is necessary to support the potential multiple invocations of the
        // lazy child node.
        let clock = Arc::new(
            primary
                .clock
                .duplicate_handle(zx::Rights::READ)
                .expect("failed to duplicate UTC clock"),
        );

        // Record fixed data directly into the node without retaining any references.
        node.record_child("initialization", |child| TimeSet::now(&clock).record(child));
        let backstop = clock.get_details().expect("failed to get clock details").backstop;
        node.record_int("backstop", backstop.into_nanos());

        let mut time_sources_hashmap = HashMap::new();
        let mut tracks_hashmap = HashMap::new();
        time_sources_hashmap.insert(
            Role::Primary,
            TimeSourceNode::new(node.create_child("primary_time_source"), &primary.time_source),
        );
        tracks_hashmap.insert(Track::Primary, TrackNode::new(node.create_child("primary_track")));

        if let Some(monitor) = optional_monitor {
            time_sources_hashmap.insert(
                Role::Monitor,
                TimeSourceNode::new(node.create_child("monitor_time_source"), &monitor.time_source),
            );
            tracks_hashmap
                .insert(Track::Monitor, TrackNode::new(node.create_child("monitor_track")));
        }

        let diagnostics = InspectDiagnostics {
            network_available_monotonic: Mutex::new(None),
            time_sources: Mutex::new(time_sources_hashmap),
            tracks: Mutex::new(tracks_hashmap),
            rtc: Mutex::new(None),
            last_update: Mutex::new(None),
            clock: Arc::clone(&clock),
            node: node.clone_weak(),
            health: Mutex::new(fuchsia_inspect::health::Node::new(node)),
        };
        diagnostics.health.lock().set_starting_up();
        node.record_lazy_child("current", move || {
            let clock_clone = Arc::clone(&clock);
            async move {
                let inspector = Inspector::new();
                TimeSet::now(&clock_clone).record(inspector.root());
                Ok(inspector)
            }
            .boxed()
        });
        diagnostics
    }

    /// Records an update to the UTC zx::Clock
    fn update_clock(&self, track: Track, reason: Option<ClockUpdateReason>) {
        if track == Track::Primary {
            self.health.lock().set_ok();
            match self.clock.get_details() {
                Ok(details) => {
                    let mut details_struct: ClockDetails = details.into();
                    if let Some(reason) = reason {
                        details_struct = details_struct.with_reason(reason);
                    }
                    let mut lock = self.last_update.lock();
                    if let Some(last_update) = &*lock {
                        last_update.update(&details_struct);
                    } else {
                        lock.replace(details_struct.create(self.node.create_child("last_update")));
                    }
                }
                Err(err) => {
                    warn!("Failed to export clock update to inspect: {}", err);
                    return;
                }
            };
        }
    }
}

impl Diagnostics for InspectDiagnostics {
    fn record(&self, event: Event) {
        match event {
            Event::Initialized { .. } => {}
            Event::NetworkAvailable => {
                self.network_available_monotonic.lock().get_or_insert_with(|| {
                    self.node.create_int("network_available_monotonic", monotonic_time())
                });
            }
            Event::InitializeRtc { outcome, time } => {
                self.rtc.lock().get_or_insert_with(|| {
                    RealTimeClockNode::new(self.node.create_child("real_time_clock"), outcome, time)
                });
            }
            Event::TimeSourceFailed { role, error } => {
                self.time_sources.lock().get_mut(&role).map(|source| source.failure(error));
            }
            Event::TimeSourceStatus { role, status } => {
                self.time_sources.lock().get_mut(&role).map(|source| source.status(status));
            }
            Event::SampleRejected { role, error } => {
                self.time_sources
                    .lock()
                    .get_mut(&role)
                    .map(|source| source.sample_rejection(error));
            }
            Event::EstimateUpdated { track, offset, sqrt_covariance } => {
                self.tracks
                    .lock()
                    .get_mut(&track)
                    .map(|track| track.update_estimate(offset, sqrt_covariance));
            }
            Event::ClockCorrection { track, correction, strategy } => {
                self.tracks
                    .lock()
                    .get_mut(&track)
                    .map(|track| track.clock_correction(correction, strategy));
            }
            Event::WriteRtc { outcome } => {
                if let Some(ref mut rtc_node) = *self.rtc.lock() {
                    rtc_node.write(outcome);
                }
            }
            Event::StartClock { track, .. } => self.update_clock(track, None),
            Event::UpdateClock { track, reason } => self.update_clock(track, Some(reason)),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            enums::{SampleValidationError as SVE, TimeSourceError as TSE},
            time_source::FakeTimeSource,
            Notifier,
        },
        fidl_fuchsia_time as ftime,
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty},
    };

    const BACKSTOP_TIME: i64 = 111111111;
    const RTC_INITIAL_TIME: i64 = 111111234;
    const RATE_ADJUST: u32 = 222;
    const ERROR_BOUNDS: u64 = 4444444444;
    const GENERATION_COUNTER: u32 = 7777;
    const OFFSET: zx::Duration = zx::Duration::from_seconds(311);
    const CORRECTION: zx::Duration = zx::Duration::from_millis(88);
    const SQRT_COVARIANCE: i64 = 5454545454;

    const VALID_DETAILS: zx::sys::zx_clock_details_v1_t = zx::sys::zx_clock_details_v1_t {
        options: 0,
        backstop_time: BACKSTOP_TIME,
        ticks_to_synthetic: zx::sys::zx_clock_transformation_t {
            reference_offset: 777777777777,
            synthetic_offset: 787878787878,
            rate: zx::sys::zx_clock_rate_t { reference_ticks: 1_000, synthetic_ticks: 1_000 },
        },
        mono_to_synthetic: zx::sys::zx_clock_transformation_t {
            reference_offset: 888888888888,
            synthetic_offset: 898989898989,
            rate: zx::sys::zx_clock_rate_t {
                reference_ticks: ONE_MILLION,
                synthetic_ticks: RATE_ADJUST as u32 + ONE_MILLION,
            },
        },
        error_bound: ERROR_BOUNDS,
        query_ticks: 12345789,
        last_value_update_ticks: 36363636,
        last_rate_adjust_update_ticks: 37373737,
        last_error_bounds_update_ticks: 38383838,
        generation_counter: GENERATION_COUNTER,
        padding1: [0, 0, 0, 0],
    };

    /// Creates a new wrapped clock set to backstop time.
    fn create_clock() -> zx::Clock {
        zx::Clock::create(zx::ClockOpts::empty(), Some(zx::Time::from_nanos(BACKSTOP_TIME)))
            .unwrap()
    }

    /// Creates a new `InspectDiagnostics` object recording to the root of the supplied inspector,
    /// returning a tuple of the object and the primary clock it is using.
    fn create_test_object(
        inspector: &Inspector,
        include_monitor: bool,
    ) -> (InspectDiagnostics, zx::Clock) {
        let primary = PrimaryTrack {
            time_source: FakeTimeSource::failing(),
            clock: create_clock(),
            notifier: Notifier::new(ftime::UtcSource::Backstop),
        };
        let monitor = match include_monitor {
            true => {
                Some(MonitorTrack { time_source: FakeTimeSource::failing(), clock: create_clock() })
            }
            false => None,
        };

        (InspectDiagnostics::new(inspector.root(), &primary, &monitor), primary.clock)
    }

    #[test]
    fn valid_clock_details_conversion() {
        let details = ClockDetails::from(zx::ClockDetails::from(VALID_DETAILS));
        assert_eq!(details.generation_counter, GENERATION_COUNTER);
        assert_eq!(details.utc_offset, VALID_DETAILS.mono_to_synthetic.synthetic_offset);
        assert_eq!(details.monotonic_offset, VALID_DETAILS.mono_to_synthetic.reference_offset);
        assert_eq!(details.rate_ppm, ONE_MILLION + RATE_ADJUST);
        assert_eq!(details.error_bounds, ERROR_BOUNDS);
    }

    #[test]
    fn invalid_clock_details_conversion() {
        let mut zx_details = zx::ClockDetails::from(VALID_DETAILS);
        zx_details.mono_to_synthetic.rate.synthetic_ticks = 1000;
        zx_details.mono_to_synthetic.rate.reference_ticks = 0;
        let details = ClockDetails::from(zx::ClockDetails::from(zx_details));
        assert_eq!(details.generation_counter, GENERATION_COUNTER);
        assert_eq!(details.utc_offset, VALID_DETAILS.mono_to_synthetic.synthetic_offset);
        assert_eq!(details.monotonic_offset, VALID_DETAILS.mono_to_synthetic.reference_offset);
        assert_eq!(details.rate_ppm, std::u32::MAX);
        assert_eq!(details.error_bounds, ERROR_BOUNDS);
    }

    #[test]
    fn after_initialization() {
        let inspector = &Inspector::new();
        let (_inspect_diagnostics, _) = create_test_object(&inspector, false);
        assert_inspect_tree!(
            inspector,
            root: contains {
                initialization: contains {
                    monotonic: AnyProperty,
                    kernel_utc: AnyProperty,
                    clock_utc: AnyProperty,
                },
                backstop: BACKSTOP_TIME,
                current: contains {
                    monotonic: AnyProperty,
                    kernel_utc: AnyProperty,
                    clock_utc: AnyProperty,
                },
                primary_time_source: contains {
                    component: "FakeTimeSource",
                    status: "Launched",
                    status_change_monotonic: AnyProperty,
                },
                primary_track: contains {
                    estimate_0: contains {
                        counter: 0u64,
                        monotonic: 0i64,
                        offset: 0i64,
                        sqrt_covariance: 0u64,
                    }
                    // For brevity we omit the other empty estimates we expect in the circular
                    // buffer.
                },
                "fuchsia.inspect.Health": contains {
                    status: "STARTING_UP",
                }
            }
        );
    }

    #[test]
    fn after_update() {
        let inspector = &Inspector::new();
        let (inspect_diagnostics, clock) = create_test_object(&inspector, false);

        // Record the time at which the network became available.
        inspect_diagnostics.record(Event::NetworkAvailable);

        // Perform two updates to the clock. The inspect data should reflect the most recent.
        clock
            .update(
                zx::ClockUpdate::new()
                    .value(zx::Time::from_nanos(BACKSTOP_TIME + 1234))
                    .rate_adjust(0)
                    .error_bounds(0),
            )
            .expect("Failed to update test clock");
        inspect_diagnostics.update_clock(Track::Primary, None);
        clock
            .update(
                zx::ClockUpdate::new()
                    .value(zx::Time::from_nanos(BACKSTOP_TIME + 2345))
                    .rate_adjust(RATE_ADJUST as i32)
                    .error_bounds(ERROR_BOUNDS),
            )
            .expect("Failed to update test clock");
        inspect_diagnostics.update_clock(Track::Primary, Some(ClockUpdateReason::TimeStep));
        assert_inspect_tree!(
            inspector,
            root: contains {
                initialization: contains {
                    monotonic: AnyProperty,
                    kernel_utc: AnyProperty,
                    clock_utc: AnyProperty,
                },
                backstop: BACKSTOP_TIME,
                network_available_monotonic: AnyProperty,
                current: contains {
                    monotonic: AnyProperty,
                    kernel_utc: AnyProperty,
                    clock_utc: AnyProperty,
                },
                last_update: contains {
                    retrieval_monotonic: AnyProperty,
                    generation_counter: 4u64,
                    monotonic_offset: AnyProperty,
                    utc_offset: AnyProperty,
                    rate_ppm: 1_000_000u64 + RATE_ADJUST as u64,
                    error_bounds: ERROR_BOUNDS,
                    reason: "Some(TimeStep)",
                },
                "fuchsia.inspect.Health": contains {
                    status: "OK",
                },
            }
        );
    }

    #[test]
    fn real_time_clock() {
        let inspector = &Inspector::new();
        let (inspect_diagnostics, _) = create_test_object(&inspector, false);
        inspect_diagnostics.record(Event::InitializeRtc {
            outcome: InitializeRtcOutcome::Succeeded,
            time: Some(zx::Time::from_nanos(RTC_INITIAL_TIME)),
        });
        assert_inspect_tree!(
            inspector,
            root: contains {
                real_time_clock: contains {
                    initialization: "Succeeded",
                    initial_time: RTC_INITIAL_TIME,
                    write_successes: 0u64,
                    write_failures: 0u64,
                }
            }
        );

        inspect_diagnostics.record(Event::WriteRtc { outcome: WriteRtcOutcome::Succeeded });
        inspect_diagnostics.record(Event::WriteRtc { outcome: WriteRtcOutcome::Succeeded });
        inspect_diagnostics.record(Event::WriteRtc { outcome: WriteRtcOutcome::Failed });
        assert_inspect_tree!(
            inspector,
            root: contains {
                real_time_clock: contains {
                    initialization: "Succeeded",
                    initial_time: RTC_INITIAL_TIME,
                    write_successes: 2u64,
                    write_failures: 1u64,
                }
            }
        );
    }

    #[test]
    fn time_sources() {
        let inspector = &Inspector::new();
        let (test, _) = create_test_object(&inspector, true);
        assert_inspect_tree!(
            inspector,
            root: contains {
                primary_time_source: contains {
                    component: "FakeTimeSource",
                    status: "Launched",
                    status_change_monotonic: AnyProperty,
                },
                monitor_time_source: contains {
                    component: "FakeTimeSource",
                    status: "Launched",
                    status_change_monotonic: AnyProperty,
                }
            }
        );

        test.record(Event::TimeSourceFailed { role: Role::Primary, error: TSE::LaunchFailed });
        test.record(Event::TimeSourceFailed { role: Role::Primary, error: TSE::CallFailed });
        test.record(Event::TimeSourceStatus { role: Role::Primary, status: Status::Ok });
        test.record(Event::SampleRejected { role: Role::Primary, error: SVE::BeforeBackstop });
        test.record(Event::TimeSourceFailed { role: Role::Primary, error: TSE::CallFailed });
        test.record(Event::TimeSourceStatus { role: Role::Monitor, status: Status::Network });

        assert_inspect_tree!(
            inspector,
            root: contains {
                primary_time_source: contains {
                    component: "FakeTimeSource",
                    status: "Failed(CallFailed)",
                    status_change_monotonic: AnyProperty,
                    failure_count_LaunchFailed: 1u64,
                    failure_count_CallFailed: 2u64,
                    rejection_count_BeforeBackstop: 1u64,
                },
                monitor_time_source: contains {
                    component: "FakeTimeSource",
                    status: "Network",
                    status_change_monotonic: AnyProperty,
                }
            }
        );
    }

    #[test]
    fn tracks() {
        let inspector = &Inspector::new();
        let (test, _) = create_test_object(&inspector, true);
        assert_inspect_tree!(
            inspector,
            root: contains {
                primary_track: contains {
                    estimate_0: contains {
                        counter: 0u64,
                        monotonic: 0i64,
                        offset: 0i64,
                        sqrt_covariance: 0u64,
                    },
                    estimate_1: contains {
                        counter: 0u64,
                        monotonic: 0i64,
                        offset: 0i64,
                        sqrt_covariance: 0u64,
                    },
                    estimate_2: contains {
                        counter: 0u64,
                        monotonic: 0i64,
                        offset: 0i64,
                        sqrt_covariance: 0u64,
                    },
                    estimate_3: contains {
                        counter: 0u64,
                        monotonic: 0i64,
                        offset: 0i64,
                        sqrt_covariance: 0u64,
                    },
                    estimate_4: contains {
                        counter: 0u64,
                        monotonic: 0i64,
                        offset: 0i64,
                        sqrt_covariance: 0u64,
                    },
                    clock_correction_0: contains {
                        counter: 0u64,
                        monotonic: 0i64,
                        correction: 0i64,
                        strategy: "NotRequired",
                    },
                    clock_correction_1: contains {
                        counter: 0u64,
                        monotonic: 0i64,
                        correction: 0i64,
                        strategy: "NotRequired",
                    },
                    clock_correction_2: contains {
                        counter: 0u64,
                        monotonic: 0i64,
                        correction: 0i64,
                        strategy: "NotRequired",
                    },
                },
                monitor_track: contains {
                    estimate_0: contains {},
                    estimate_1: contains {},
                    estimate_2: contains {},
                    estimate_3: contains {},
                    estimate_4: contains {},
                    clock_correction_0: contains {},
                    clock_correction_1: contains {},
                    clock_correction_2: contains {},
                },
            }
        );

        // Write enough to wrap the circular buffer
        for i in 1..8 {
            test.record(Event::EstimateUpdated {
                track: Track::Primary,
                offset: OFFSET * i,
                sqrt_covariance: zx::Duration::from_nanos(SQRT_COVARIANCE) * i,
            });
            test.record(Event::ClockCorrection {
                track: Track::Primary,
                correction: CORRECTION * i,
                strategy: ClockCorrectionStrategy::MaxDurationSlew,
            });
        }

        assert_inspect_tree!(
            inspector,
            root: contains {
                primary_track: contains {
                    estimate_0: contains {
                        counter: 6u64,
                        monotonic: AnyProperty,
                        offset: 6 * OFFSET.into_nanos(),
                        sqrt_covariance: 6 * SQRT_COVARIANCE as u64,
                    },
                    estimate_1: contains {
                        counter: 7u64,
                        monotonic: AnyProperty,
                        offset: 7 * OFFSET.into_nanos(),
                        sqrt_covariance: 7 * SQRT_COVARIANCE as u64,
                    },
                    estimate_2: contains {
                        counter: 3u64,
                        monotonic: AnyProperty,
                        offset: 3 * OFFSET.into_nanos(),
                        sqrt_covariance: 3 * SQRT_COVARIANCE as u64,
                    },
                    estimate_3: contains {
                        counter: 4u64,
                        monotonic: AnyProperty,
                        offset: 4 * OFFSET.into_nanos(),
                        sqrt_covariance: 4 * SQRT_COVARIANCE as u64,
                    },
                    estimate_4: contains {
                        counter: 5u64,
                        monotonic: AnyProperty,
                        offset: 5 * OFFSET.into_nanos(),
                        sqrt_covariance: 5 * SQRT_COVARIANCE as u64,
                    },
                    clock_correction_0: contains {
                        counter: 7u64,
                        monotonic: AnyProperty,
                        correction: 7 * CORRECTION.into_nanos(),
                        strategy: "MaxDurationSlew",
                    },
                    clock_correction_1: contains {
                        counter: 5u64,
                        monotonic: AnyProperty,
                        correction: 5 * CORRECTION.into_nanos(),
                        strategy: "MaxDurationSlew",
                    },
                    clock_correction_2: contains {
                        counter: 6u64,
                        monotonic: AnyProperty,
                        correction: 6 * CORRECTION.into_nanos(),
                        strategy: "MaxDurationSlew",
                    },
                },
                monitor_track: contains {
                    estimate_0: contains {},
                    estimate_1: contains {},
                    estimate_2: contains {},
                    estimate_3: contains {},
                    estimate_4: contains {},
                },
            }
        );
    }

    #[test]
    fn health() {
        let inspector = &Inspector::new();
        let _inspect_diagnostics = create_test_object(&inspector, false);
        assert_inspect_tree!(
            inspector,
            root: contains {
                "fuchsia.inspect.Health": contains {
                    status: "STARTING_UP",
                },
            }
        );
    }
}
