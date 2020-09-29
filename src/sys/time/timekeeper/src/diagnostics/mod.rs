// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cobalt;
mod composite;
#[cfg(test)]
mod fake;
mod inspect;

pub use self::cobalt::CobaltDiagnostics;
pub use self::composite::CompositeDiagnostics;
#[cfg(test)]
pub use self::fake::FakeDiagnostics;
pub use self::inspect::{InspectDiagnostics, INSPECTOR};

use {
    crate::enums::{
        InitialClockState, InitializeRtcOutcome, Role, SampleValidationError, StartClockSource,
        TimeSourceError, WriteRtcOutcome,
    },
    fidl_fuchsia_time_external::Status,
    fuchsia_zircon as zx,
};

/// An event that is potantialle worth recording in one or more diagnostics systems.
#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    /// Timekeeper has completed initialization.
    Initialized { clock_state: InitialClockState },
    /// A network with Internet connectivity became available for the first time.
    NetworkAvailable,
    /// An attempt was made to intitialize and read from the real time clock.
    InitializeRtc { outcome: InitializeRtcOutcome, time: Option<zx::Time> },
    /// A time source failed, relaunch will be attempted.
    TimeSourceFailed { role: Role, error: TimeSourceError },
    /// A time source changed its state.
    TimeSourceStatus { role: Role, status: Status },
    /// A sample received from a time source was rejected during validation.
    SampleRejected { role: Role, error: SampleValidationError },
    /// An attempt was made to write to the real time clock.
    WriteRtc { outcome: WriteRtcOutcome },
    /// The userspace clock has been started for the first time.
    StartClock { source: StartClockSource },
    /// The userspace clock has been updated.
    UpdateClock,
}

/// A standard interface for systems record events for diagnostic purposes.
pub trait Diagnostics {
    /// Records the supplied event if relevant.
    fn record(&self, event: Event);
}
