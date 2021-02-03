// Copyright 2020 The Fuchsia Authors. All rights reserved.
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
pub use self::inspect::InspectDiagnostics;

use crate::datatypes::{HttpsSample, Phase};
use httpdate_hyper::HttpsDateErrorType;

/// A standard interface for recording sample production attempts for diagnostic purposes.
pub trait Diagnostics: Send + Sync {
    /// Records an event.
    fn record<'a>(&self, event: Event<'a>);
}

/// An event reported for diagnostic purposes.
#[derive(Clone, Copy, PartialEq)]
pub enum Event<'a> {
    /// Completion of the network availability check.
    NetworkCheckSuccessful,
    /// A successful attempt to produce a sample.
    Success(&'a HttpsSample),
    /// A failed attempt to produce a sample.
    Failure(HttpsDateErrorType),
    /// A change in the phase.
    Phase(Phase),
}
