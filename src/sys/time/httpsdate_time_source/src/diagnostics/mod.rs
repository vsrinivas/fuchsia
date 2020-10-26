// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod fake;
mod inspect;

#[cfg(test)]
pub use self::fake::FakeDiagnostics;
pub use self::inspect::InspectDiagnostics;

use crate::datatypes::HttpsSample;
use httpdate_hyper::HttpsDateError;

/// A standard interface for recording sample production attempts for diagnostic purposes.
pub trait Diagnostics: Send + Sync {
    /// Records a successful attempt to produce a sample.
    fn success(&self, sample: &HttpsSample);
    /// Records a failed attempt to produce a sample.
    fn failure(&self, error: &HttpsDateError);
}
