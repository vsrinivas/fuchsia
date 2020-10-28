// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::datatypes::HttpsSample;
use crate::diagnostics::Diagnostics;
use httpdate_hyper::HttpsDateError;

/// A trivial `Diagnostics` implementation that simply sends every event it receives to two other
/// `Diagnostics` implementations.
pub struct CompositeDiagnostics<L: Diagnostics, R: Diagnostics> {
    /// The first `Diagnostics` implementation to receive events.
    left: L,
    /// The second `Diagnostics` implementation to receive events.
    right: R,
}

impl<L: Diagnostics, R: Diagnostics> CompositeDiagnostics<L, R> {
    /// Contructs a new `CompositeDiagnostics` instance that forwards all events to the supplied
    /// diagnostics implementations.
    pub fn new(left: L, right: R) -> Self {
        Self { left, right }
    }
}

impl<L: Diagnostics, R: Diagnostics> Diagnostics for CompositeDiagnostics<L, R> {
    fn success(&self, sample: &HttpsSample) {
        self.left.success(sample);
        self.right.success(sample);
    }

    fn failure(&self, error: &HttpsDateError) {
        self.left.failure(error);
        self.right.failure(error);
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::diagnostics::FakeDiagnostics;
    use fuchsia_zircon as zx;
    use lazy_static::lazy_static;
    use std::sync::Arc;

    lazy_static! {
        static ref TEST_SAMPLE: HttpsSample = HttpsSample {
            utc: zx::Time::from_nanos(111_111_111),
            monotonic: zx::Time::from_nanos(222_222_222),
            standard_deviation: zx::Duration::from_millis(235),
            final_bound_size: zx::Duration::from_millis(100),
            round_trip_times: vec![],
        };
    }
    const TEST_ERROR: HttpsDateError = HttpsDateError::NetworkError;

    #[test]
    fn log_successes() {
        let left = Arc::new(FakeDiagnostics::new());
        let right = Arc::new(FakeDiagnostics::new());
        let composite = CompositeDiagnostics::new(Arc::clone(&left), Arc::clone(&right));
        assert!(left.successes().is_empty());
        assert!(right.successes().is_empty());

        composite.success(&*TEST_SAMPLE);
        assert_eq!(left.successes(), vec![TEST_SAMPLE.clone()]);
        assert_eq!(right.successes(), vec![TEST_SAMPLE.clone()]);
    }

    #[test]
    fn log_failures() {
        let left = Arc::new(FakeDiagnostics::new());
        let right = Arc::new(FakeDiagnostics::new());
        let composite = CompositeDiagnostics::new(Arc::clone(&left), Arc::clone(&right));
        assert!(left.failures().is_empty());
        assert!(right.failures().is_empty());

        composite.failure(&TEST_ERROR);
        assert_eq!(left.failures(), vec![TEST_ERROR]);
        assert_eq!(right.failures(), vec![TEST_ERROR]);
    }
}
