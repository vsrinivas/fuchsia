// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, Event},
    crate::enums::{InitializeRtcOutcome, Track, WriteRtcOutcome},
    fuchsia_inspect::{
        health::Reporter, Inspector, IntProperty, Node, NumericProperty, Property, UintProperty,
    },
    fuchsia_zircon as zx,
    futures::FutureExt,
    lazy_static::lazy_static,
    log::warn,
    parking_lot::Mutex,
    std::sync::Arc,
};

const ONE_MILLION: u32 = 1_000_000;
/// The value stored in place of any time that could not be generated.
const FAILED_TIME: i64 = -1;

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
}

fn monotonic_time() -> i64 {
    zx::Time::get(zx::ClockId::Monotonic).into_nanos()
}

fn utc_time() -> i64 {
    zx::Time::get(zx::ClockId::UTC).into_nanos()
}

/// A representation of a point in time as measured by all pertinent clocks.
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

    /// Writes the contents of the `TimeSet` to the supplied node using the inspect record_* methods
    /// TODO(jsankey): This method could be placed in a trait (`Recordable`?) and generated with a
    /// procedural macro.
    pub fn record(self, node: &Node) {
        node.record_int("monotonic", self.monotonic);
        node.record_int("kernel_utc", self.kernel_utc);
        node.record_int("clock_utc", self.clock_utc);
    }
}

/// A representation of a single update to the UTC zx::Clock.
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
        }
    }
}

/// An inspect `Node` and properties used to export the contents of a `ClockDetails`.
/// TODO(jsankey): Auto generate this struct and the impl from a procedural macro.
struct ClockDetailsNode {
    /// The monotonic time at which the details were retrieved. Note this is the time the Rust
    /// object was created, which may not exactly match the time its contents were supplied by
    /// the kernel.
    _retrieval_monotonic: IntProperty,
    /// The generation counter as documented in the zx::Clock.
    _generation_counter: UintProperty,
    /// The monotonic time from the monotonic-UTC correspondence pair, in ns.
    _monotonic_offset: IntProperty,
    /// The UTC time from the monotonic-UTC correspondence pair, in ns.
    _utc_offset: IntProperty,
    /// The ratio between UTC tick rate and monotonic tick rate in parts per million, where
    /// a value above one million means UTC is ticking faster than monotonic.
    _rate_ppm: UintProperty,
    /// The error bounds as documented in the zx::Clock.
    _error_bounds: UintProperty,
    /// The inspect Node these fields are exported to.
    _node: Node,
}

impl ClockDetailsNode {
    /// Constructs a new `ClockDetails` using the inspect create_* methods (meaning the properties
    /// are bound to the lifetime of the `ClockDetailsNode`) with fields set to the supplied
    /// `ClockDetails`.
    pub fn create(node: Node, data: ClockDetails) -> Self {
        ClockDetailsNode {
            _retrieval_monotonic: node.create_int("retrieval_monotonic", data.retrieval_monotonic),
            _generation_counter: node
                .create_uint("generation_counter", data.generation_counter as u64),
            _monotonic_offset: node.create_int("monotonic_offset", data.monotonic_offset),
            _utc_offset: node.create_int("utc_offset", data.utc_offset),
            _rate_ppm: node.create_uint("rate_ppm", data.rate_ppm as u64),
            _error_bounds: node.create_uint("error_bounds", data.error_bounds),
            _node: node,
        }
    }

    /// Sets the `ClockDetailsNode` inspect fields to the contents of the supplied `ClockDetails`.
    pub fn update(&self, data: ClockDetails) {
        self._retrieval_monotonic.set(data.retrieval_monotonic);
        self._generation_counter.set(data.generation_counter as u64);
        self._monotonic_offset.set(data.monotonic_offset);
        self._utc_offset.set(data.utc_offset);
        self._rate_ppm.set(data.rate_ppm as u64);
        self._error_bounds.set(data.error_bounds);
    }
}

/// An inspect `Node` and properties used to describe interactions with a real time clock.
struct RealTimeClockNode {
    /// The number of successful writes to the RTC.
    _write_success_counter: UintProperty,
    /// The number of failed writes to the RTC.
    _write_failure_counter: UintProperty,
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
            _write_success_counter: node.create_uint("write_successes", 0u64),
            _write_failure_counter: node.create_uint("write_failures", 0u64),
            _node: node,
        }
    }

    /// Records an attempt to write to the clock.
    pub fn write(&mut self, outcome: WriteRtcOutcome) {
        match outcome {
            WriteRtcOutcome::Succeeded => self._write_success_counter.add(1),
            WriteRtcOutcome::Failed => self._write_failure_counter.add(1),
        }
    }
}

/// The complete set of Timekeeper information exported through Inspect.
pub struct InspectDiagnostics {
    /// The monotonic time at which the network became available, in nanoseconds.
    network_available_monotonic: Mutex<Option<IntProperty>>,
    /// Details of interactions with the real time clock.
    rtc: Mutex<Option<RealTimeClockNode>>,
    /// The details of the most recent update to the UTC zx::Clock.
    last_update: Mutex<Option<ClockDetailsNode>>,
    /// The UTC clock that provides the `clock_utc` component of `TimeSet` data.
    clock: Arc<zx::Clock>,
    /// The inspect node used to export the contents of this `InspectDiagnostics`.
    node: Node,
    /// The inspect health node to expose component status.
    health: Mutex<fuchsia_inspect::health::Node>,
}

impl InspectDiagnostics {
    /// Construct a new `InspectDiagnostics` exporting at the supplied `Node` using data from
    /// the supplied clock.
    pub fn new(node: &Node, clock: Arc<zx::Clock>) -> Self {
        // Record fixed data directly into the node without retaining any references.
        node.record_child("initialization", |child| TimeSet::now(&clock).record(child));
        node.record_int(
            "backstop",
            clock.get_details().map_or(FAILED_TIME, |details| details.backstop.into_nanos()),
        );

        let diagnostics = InspectDiagnostics {
            network_available_monotonic: Mutex::new(None),
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
    fn update_clock(&self, track: Track) {
        if track == Track::Primary {
            self.health.lock().set_ok();
            match self.clock.get_details() {
                Ok(details) => {
                    let mut lock = self.last_update.lock();
                    if let Some(last_update) = &*lock {
                        last_update.update(details.into());
                    } else {
                        lock.replace(ClockDetailsNode::create(
                            self.node.create_child("last_update"),
                            details.into(),
                        ));
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
            Event::TimeSourceFailed { .. } => { /* TODO(jsankey): Add implementation */ }
            Event::TimeSourceStatus { .. } => { /* TODO(jsankey): Add implementation */ }
            Event::SampleRejected { .. } => { /* TODO(jsankey): Add implementation */ }
            Event::WriteRtc { outcome } => {
                if let Some(ref mut rtc_node) = *self.rtc.lock() {
                    rtc_node.write(outcome);
                }
            }
            Event::StartClock { track, .. } | Event::UpdateClock { track } => {
                self.update_clock(track)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty},
    };

    const BACKSTOP_TIME: i64 = 111111111;
    const RTC_INITIAL_TIME: i64 = 111111234;
    const RATE_ADJUST: u32 = 222;
    const ERROR_BOUNDS: u64 = 4444444444;
    const GENERATION_COUNTER: u32 = 7777;

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

    /// Creates a new arc wrapped clock set to backstop time.
    fn create_clock() -> Arc<zx::Clock> {
        Arc::new(
            zx::Clock::create(zx::ClockOpts::empty(), Some(zx::Time::from_nanos(BACKSTOP_TIME)))
                .unwrap(),
        )
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
        let _inspect_diagnostics = InspectDiagnostics::new(inspector.root(), create_clock());
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
                "fuchsia.inspect.Health": contains {
                    status: "STARTING_UP",
                }
            }
        );
    }

    #[test]
    fn after_update() {
        let clock = create_clock();
        let inspector = &Inspector::new();
        let inspect_diagnostics = InspectDiagnostics::new(inspector.root(), Arc::clone(&clock));

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
        inspect_diagnostics.update_clock(Track::Primary);
        clock
            .update(
                zx::ClockUpdate::new()
                    .value(zx::Time::from_nanos(BACKSTOP_TIME + 2345))
                    .rate_adjust(RATE_ADJUST as i32)
                    .error_bounds(ERROR_BOUNDS),
            )
            .expect("Failed to update test clock");
        inspect_diagnostics.update_clock(Track::Primary);
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
        let inspect_diagnostics = InspectDiagnostics::new(inspector.root(), create_clock());
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
    fn health() {
        let inspector = &Inspector::new();
        let _inspect_diagnostics = InspectDiagnostics::new(inspector.root(), create_clock());
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
