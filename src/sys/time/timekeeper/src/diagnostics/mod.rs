// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cobalt;
mod inspect;

#[cfg(test)]
pub use self::cobalt::fake::{FakeCobaltDiagnostics, FakeCobaltMonitor};
pub use self::cobalt::{CobaltDiagnostics, CobaltDiagnosticsImpl};
pub use self::inspect::{InspectDiagnostics, INSPECTOR};

use {
    crate::enums::{InitializeRtcOutcome, WriteRtcOutcome},
    fuchsia_zircon as zx,
};

/// An event that is potantialle worth recording in one or more diagnostics systems.
pub enum Event<'a> {
    /// A network with Internet connectivity became available for the first time.
    NetworkAvailable,
    /// An attempt was made to intitialize and read from the real time clock.
    InitializeRtc { outcome: InitializeRtcOutcome, time: Option<zx::Time> },
    /// An attempt was made to write to the real time clock.
    WriteRtc { outcome: WriteRtcOutcome },
    /// The userspace clock has been updated.
    UpdateClock,
    /// Timekeeper has failed in some permanent fashion.
    Failure { reason: &'a str },
}

/// A standard interface for systems record events for diagnostic purposes.
pub trait Diagnostics {
    /// Records the supplied event if relevant.
    fn record(&self, event: Event<'_>);
}
