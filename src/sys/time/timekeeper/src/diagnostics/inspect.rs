// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{health::Reporter, Inspector, Node},
    fuchsia_inspect_derive::{IValue, Inspect},
    fuchsia_zircon as zx,
    futures::FutureExt,
    lazy_static::lazy_static,
    log::warn,
    std::sync::Arc,
};

const ONE_MILLION: u32 = 1_000_000;
/// The value stored in place of any time that could not be generated.
const FAILED_TIME: i64 = -1;

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
}

/// Attempts to call `iattach` to attach `$inspectable` under node `$parent` with name `$name`,
/// logging a warning on failure.
macro_rules! attempt_iattach {
    ($inspectable:expr, $parent:expr, $name:expr) => {
        if let Err(err) = $inspectable.iattach($parent, $name) {
            warn!("Failed to attach inspect node {}: {}", $name, err);
        }
    };
}

fn monotonic_time() -> i64 {
    zx::Time::get(zx::ClockId::Monotonic).into_nanos()
}

fn utc_time() -> i64 {
    zx::Time::get(zx::ClockId::UTC).into_nanos()
}

/// A representation of a point in time as measured by all pertinent clocks.
#[derive(Inspect)]
pub struct TimeSet {
    /// The kernel ZX_CLOCK_MONOTONIC time, in ns.
    monotonic: IValue<i64>,
    /// The kernel ZX_CLOCK_UTC time, in ns.
    kernel_utc: IValue<i64>,
    /// The UTC zx::Clock time, in ns.
    clock_utc: IValue<i64>,
    /// The inspect Node these fields are exported to.
    inspect_node: fuchsia_inspect::Node,
}

impl TimeSet {
    /// Returns a new `TimeSet` representing current time, using the inspect `Node` supplied by the
    /// caller where present, or creating a new one otherwise.
    pub fn now(clock: &zx::Clock, node: Option<&fuchsia_inspect::Node>) -> Self {
        TimeSet {
            monotonic: IValue::new(monotonic_time()),
            kernel_utc: IValue::new(utc_time()),
            clock_utc: IValue::new(clock.read().map(zx::Time::into_nanos).unwrap_or(FAILED_TIME)),
            inspect_node: node
                .map_or_else(fuchsia_inspect::Node::default, |node| node.clone_weak()),
        }
    }

    /// Uses the Inspect record_* API to store the contents of this `TimeSet` to its node. Unlike
    /// the create_* API used by the Inspect trait implementation, this lets the fields outlive the
    /// livetime of the `TimeSet` and is useful when asynchronously adding lazy children.
    pub fn record(&self) {
        self.inspect_node.record_int("monotonic", *self.monotonic);
        self.inspect_node.record_int("kernel_utc", *self.kernel_utc);
        self.inspect_node.record_int("clock_utc", *self.clock_utc);
    }
}

/// A representation of a single update to the UTC zx::Clock for export via Inspect.
#[derive(Default, Inspect)]
pub struct ClockDetails {
    /// The monotonic time at which the details were retrieved. Note this is the time the Rust
    /// object was created, which may not exactly match the time its contents were supplied by
    /// the kernel.
    retrieval_monotonic: IValue<i64>,
    /// The generation counter as documented in the zx::Clock.
    generation_counter: IValue<u32>,
    /// The monotonic time from the monotonic-UTC correspondence pair, in ns.
    monotonic_offset: IValue<i64>,
    /// The UTC time from the monotonic-UTC correspondence pair, in ns.
    utc_offset: IValue<i64>,
    /// The ratio between UTC tick rate and monotonic tick rate in parts per million, where
    /// a value above one million means UTC is ticking faster than monotonic.
    rate_ppm: IValue<u32>,
    /// The error bounds as documented in the zx::Clock.
    error_bounds: IValue<u64>,
    /// The inspect Node these fields are exported to.
    inspect_node: fuchsia_inspect::Node,
}

impl ClockDetails {
    /// Sets the content of this `ClockDetails` based on the supplied `zx::ClockDetails`.
    pub fn set(&mut self, details: zx::ClockDetails) {
        // Handle the potential for a divide by zero in an unset rate.
        let rate_ppm = match (
            details.mono_to_synthetic.rate.synthetic_ticks,
            details.mono_to_synthetic.rate.reference_ticks,
        ) {
            (0, _) => 0,
            (_, 0) => std::i64::MAX,
            (synthetic, reference) => (synthetic as i64 * ONE_MILLION as i64) / (reference as i64),
        };
        self.retrieval_monotonic.iset(monotonic_time());
        self.generation_counter.iset(details.generation_counter);
        self.monotonic_offset.iset(details.mono_to_synthetic.reference_offset);
        self.utc_offset.iset(details.mono_to_synthetic.synthetic_offset);
        self.rate_ppm.iset(rate_ppm as u32);
        self.error_bounds.iset(details.error_bounds);
    }
}

impl From<zx::ClockDetails> for ClockDetails {
    fn from(details: zx::ClockDetails) -> ClockDetails {
        let mut ret = ClockDetails::default();
        ret.set(details);
        ret
    }
}

/// The complete set of Timekeeper information exported through Inspect.
// Note: The inspect trait is implemented manually since some nodes are lazily created.
pub struct InspectDiagnostics {
    /// The clock times at initialization.
    initialization: TimeSet,
    /// The backstop time in nanoseconds.
    backstop: IValue<i64>,
    /// The monotonic time at which the network became available, in nanoseconds.
    network_available_monotonic: Option<IValue<i64>>,
    /// The details of the most recent update to the UTC zx::Clock.
    last_update: Option<ClockDetails>,
    /// The UTC clock that provides the `clock_utc` component of `TimeSet` data.
    clock: Arc<zx::Clock>,
    /// The inspect node used to export the contents of this `InspectDiagnostics`.
    node: Node,
    /// The inspect health node to expose component status.
    health: fuchsia_inspect::health::Node,
}

impl InspectDiagnostics {
    /// Construct a new `InspectDiagnostics` exporting at the supplied `Node` using data from
    /// the supplied clock.
    pub fn new(node: &Node, clock: Arc<zx::Clock>) -> Self {
        let initialization = TimeSet::now(&clock, None);
        let backstop = IValue::new(
            clock.get_details().map_or(FAILED_TIME, |details| details.backstop.into_nanos()),
        );
        let mut diagnostics = InspectDiagnostics {
            initialization,
            backstop,
            network_available_monotonic: None,
            last_update: None,
            clock: Arc::clone(&clock),
            node: node.clone_weak(),
            health: fuchsia_inspect::health::Node::new(node),
        };
        diagnostics.health.set_starting_up();
        // Following the general inspect API philosophy we make our best effort at exporting data
        // without raising errors that would lead branching logic (i.e. "if inspect is available do
        // this, otherwise do that" in the operational code.
        attempt_iattach!(diagnostics.initialization, &node, "initialization");
        attempt_iattach!(diagnostics.backstop, &node, "backstop");
        node.record_lazy_child("current", move || {
            let clock_clone = Arc::clone(&clock);
            async move {
                let inspector = Inspector::new();
                TimeSet::now(&clock_clone, Some(&inspector.root())).record();
                Ok(inspector)
            }
            .boxed()
        });
        diagnostics
    }

    /// Records the fact that network is now available.
    pub fn network_available(&mut self) {
        if self.network_available_monotonic.is_none() {
            let mut monotonic = IValue::new(monotonic_time());
            attempt_iattach!(monotonic, &self.node, "network_available_monotonic");
            self.network_available_monotonic = Some(monotonic);
        }
    }

    /// Records an update to the UTC zx::Clock
    pub fn update_clock(&mut self) {
        self.health.set_ok();
        match self.clock.get_details() {
            Ok(details) => {
                if let Some(last_update) = &mut self.last_update {
                    last_update.set(details);
                } else {
                    let mut update = ClockDetails::from(details);
                    attempt_iattach!(update, &self.node, "last_update");
                    self.last_update = Some(update);
                }
            }
            Err(err) => {
                warn!("Failed to export clock update to inspect: {}", err);
                return;
            }
        };
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty},
    };

    const BACKSTOP_TIME: i64 = 111111111;
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

    #[test]
    fn valid_clock_details_conversion() {
        let details = ClockDetails::from(zx::ClockDetails::from(VALID_DETAILS));
        assert_eq!(*details.generation_counter, GENERATION_COUNTER);
        assert_eq!(*details.utc_offset, VALID_DETAILS.mono_to_synthetic.synthetic_offset);
        assert_eq!(*details.monotonic_offset, VALID_DETAILS.mono_to_synthetic.reference_offset);
        assert_eq!(*details.rate_ppm, ONE_MILLION + RATE_ADJUST);
        assert_eq!(*details.error_bounds, ERROR_BOUNDS);
    }

    #[test]
    fn invalid_clock_details_conversion() {
        let mut zx_details = zx::ClockDetails::from(VALID_DETAILS);
        zx_details.mono_to_synthetic.rate.synthetic_ticks = 1000;
        zx_details.mono_to_synthetic.rate.reference_ticks = 0;
        let details = ClockDetails::from(zx::ClockDetails::from(zx_details));
        assert_eq!(*details.generation_counter, GENERATION_COUNTER);
        assert_eq!(*details.utc_offset, VALID_DETAILS.mono_to_synthetic.synthetic_offset);
        assert_eq!(*details.monotonic_offset, VALID_DETAILS.mono_to_synthetic.reference_offset);
        assert_eq!(*details.rate_ppm, std::u32::MAX);
        assert_eq!(*details.error_bounds, ERROR_BOUNDS);
    }

    #[test]
    fn after_initialization() {
        let dummy_clock = Arc::new(
            zx::Clock::create(zx::ClockOpts::empty(), Some(zx::Time::from_nanos(BACKSTOP_TIME)))
                .unwrap(),
        );
        let inspector = &Inspector::new();
        let _inspect_diagnostics =
            InspectDiagnostics::new(inspector.root(), Arc::clone(&dummy_clock));
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
        let dummy_clock = Arc::new(
            zx::Clock::create(zx::ClockOpts::empty(), Some(zx::Time::from_nanos(BACKSTOP_TIME)))
                .unwrap(),
        );
        let inspector = &Inspector::new();
        let mut inspect_diagnostics =
            InspectDiagnostics::new(inspector.root(), Arc::clone(&dummy_clock));

        // Record the time at which the network became available.
        inspect_diagnostics.network_available();

        // Perform two updates to the clock. The inspect data should reflect the most recent.
        dummy_clock
            .update(
                zx::ClockUpdate::new()
                    .value(zx::Time::from_nanos(BACKSTOP_TIME + 1234))
                    .rate_adjust(0)
                    .error_bounds(0),
            )
            .expect("Failed to update test clock");
        inspect_diagnostics.update_clock();
        dummy_clock
            .update(
                zx::ClockUpdate::new()
                    .value(zx::Time::from_nanos(BACKSTOP_TIME + 2345))
                    .rate_adjust(RATE_ADJUST as i32)
                    .error_bounds(ERROR_BOUNDS),
            )
            .expect("Failed to update test clock");
        inspect_diagnostics.update_clock();
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
}
