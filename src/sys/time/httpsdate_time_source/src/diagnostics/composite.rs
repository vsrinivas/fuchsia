// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::diagnostics::{Diagnostics, Event};

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
    fn record<'a>(&self, event: Event<'a>) {
        self.left.record(event);
        self.right.record(event);
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::datatypes::{HttpsSample, Phase};
    use crate::diagnostics::FakeDiagnostics;
    use fuchsia_zircon as zx;
    use httpdate_hyper::HttpsDateErrorType;
    use lazy_static::lazy_static;
    use std::sync::Arc;

    lazy_static! {
        static ref TEST_SAMPLE: HttpsSample = HttpsSample {
            utc: zx::Time::from_nanos(111_111_111),
            monotonic: zx::Time::from_nanos(222_222_222),
            standard_deviation: zx::Duration::from_millis(235),
            final_bound_size: zx::Duration::from_millis(100),
            polls: vec![],
        };
        static ref TEST_SUCCESS: Event<'static> = Event::Success(&*TEST_SAMPLE);
    }
    const TEST_FAILURE: Event<'static> = Event::Failure(HttpsDateErrorType::NetworkError);
    const TEST_PHASE: Event<'static> = Event::Phase(Phase::Converge);

    #[fuchsia::test]
    fn record_events() {
        let left = Arc::new(FakeDiagnostics::new());
        let right = Arc::new(FakeDiagnostics::new());
        let composite = CompositeDiagnostics::new(Arc::clone(&left), Arc::clone(&right));
        left.assert_events(vec![]);
        right.assert_events(vec![]);

        composite.record(*TEST_SUCCESS);
        composite.record(TEST_FAILURE);
        composite.record(TEST_PHASE);
        left.assert_events(vec![*TEST_SUCCESS, TEST_FAILURE, TEST_PHASE]);
        right.assert_events(vec![*TEST_SUCCESS, TEST_FAILURE, TEST_PHASE]);
    }
}
