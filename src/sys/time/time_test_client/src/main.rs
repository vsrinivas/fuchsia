// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `time_test_client` is a simple component that logs diagnostic information about the time it
//! receives from the system to aid in debugging and (potentially in the future) automated testing.

use {
    anyhow::{Context as _, Error},
    chrono::prelude::*,
    fuchsia_async as fasync, fuchsia_runtime as runtime, fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    log::{info, warn},
};

/// Delay between polls of system and userspace clocks.
const POLL_DELAY: zx::Duration = zx::Duration::from_seconds(2);

lazy_static! {
   /// Rights to request when duplicating handles to userspace clocks.
   static ref CLOCK_RIGHTS: zx::Rights = zx::Rights::READ | zx::Rights::DUPLICATE;
}

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["time"]).context("initializing logging").unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);

    let mut futures = vec![];
    futures.push(RuntimeUtcMonitor::new().execute().boxed());
    match ClockMonitor::new() {
        Ok(clock_monitor) => futures.push(clock_monitor.execute().boxed()),
        Err(err) => warn!("{}", err),
    }
    // TODO(jsankey): Add a monitor for the fuchsia.time.Utc FIDL interface.
    future::join_all(futures).await;
}

/// A monitor for UTC as reported by the runtime.
struct RuntimeUtcMonitor {
    /// The UTC time when this monitor was initialized.
    initial: DateTime<Utc>,
}

impl RuntimeUtcMonitor {
    /// Creates a new `UtcMonitor`, logging the initial state.
    pub fn new() -> Self {
        let initial = Utc::now();
        info!("Runtime UTC at initialization: {}", initial.to_rfc2822());
        RuntimeUtcMonitor { initial }
    }

    /// Async function to operate this monitor.
    async fn execute(self) {
        let mut last_logged = self.initial;
        loop {
            fasync::Timer::new(fasync::Time::after(POLL_DELAY)).await;
            // Only log UTC when we reach a new minute.
            let current = Utc::now();
            if current.hour() != last_logged.hour() || current.minute() != last_logged.minute() {
                info!("Runtime UTC: {}", current.to_rfc2822());
                last_logged = current;
            }
        }
    }
}

/// A monitor for a UTC `zx::Clock` to log changes in clock details.
struct ClockMonitor {
    /// The `zx::Clock` to be monitored.
    clock: zx::Clock,
    /// The generation counter that was present during initialization.
    initial_generation: u32,
}

impl ClockMonitor {
    /// Creates a new `ClockMonitor`, logging the initial state, or returns an error if a clock
    /// could not be found.
    pub fn new() -> Result<Self, Error> {
        // Retrieve a handle to the UTC clock.
        let clock = runtime::duplicate_utc_clock_handle(*CLOCK_RIGHTS)
            .map_err(|stat| anyhow::anyhow!("Error retreiving UTC clock handle: {}", stat))?;

        // Log the time reported by the clock.
        match clock.read() {
            Ok(time) => info!(
                "Clock UTC at initialization: {}",
                Utc.timestamp_nanos(time.into_nanos()).to_rfc2822()
            ),
            Err(stat) => warn!("Error reading initial clock time {}", stat),
        }

        // Log the initial details and backstop time on creation
        match clock.get_details() {
            Ok(details) => {
                info!(
                    "Clock backstop time: {}",
                    Utc.timestamp_nanos(details.backstop.into_nanos()).to_rfc2822()
                );
                info!("Details at initialization: {}", Self::describe_clock_details(&details));
                Ok(ClockMonitor { clock, initial_generation: details.generation_counter })
            }
            Err(stat) => {
                warn!("Error reading clock details {}", stat);
                Ok(ClockMonitor { clock, initial_generation: 0 })
            }
        }
    }

    /// Async function to operate this monitor, returning immediately if the clock was not found.
    async fn execute(self) {
        // Future to log when the clock started signal is observed.
        let clock_start_fut = async {
            match fasync::OnSignals::new(&self.clock, zx::Signals::CLOCK_STARTED).await {
                Ok(_) => info!("UTC zx:Clock has started"),
                Err(err) => warn!("Error waiting for clock start: {}", err),
            }
        };

        // Future to log every time a new generation_counter is seen (note we don't print errors on
        // failing to read details, such a failure is likely persistent and would be spammy).
        let generation_change_fut = async {
            let mut last_logged = self.initial_generation;
            loop {
                fasync::Timer::new(fasync::Time::after(POLL_DELAY)).await;
                if let Ok(details) = self.clock.get_details() {
                    if details.generation_counter != last_logged {
                        info!("Updated details: {}", Self::describe_clock_details(&details));
                        last_logged = details.generation_counter;
                    }
                }
            }
        };

        future::join(clock_start_fut, generation_change_fut).await;
    }

    /// Returns a string description of the most useful information in a `zx::ClockDetails`.
    fn describe_clock_details(clock_details: &zx::ClockDetails) -> String {
        let transform = &clock_details.mono_to_synthetic;
        format!(
            "gen={}, offset={:.1}s, rate={}/{}, err_bound={:.3}ms",
            clock_details.generation_counter,
            (transform.synthetic_offset - transform.reference_offset) as f64 / 1e9f64,
            transform.rate.synthetic_ticks,
            transform.rate.reference_ticks,
            (clock_details.error_bounds as f64) / 1e6f64,
        )
    }
}
