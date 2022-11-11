// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::datatypes::{HttpsSample, Phase};
use crate::diagnostics::{Diagnostics, Event};
use httpdate_hyper::HttpsDateErrorType;
use parking_lot::Mutex;

/// A fake `Diagnostics` implementation useful for verifying unittests.
pub struct FakeDiagnostics {
    /// An ordered list of the events received since the last reset.
    events: Mutex<Vec<OwnedEvent>>,
}

/// A copy of `Event` where all contents are owned.
#[derive(PartialEq, Debug)]
enum OwnedEvent {
    NetworkCheckSuccessful,
    Success(HttpsSample),
    Failure(HttpsDateErrorType),
    Phase(Phase),
}

impl<'a> From<Event<'a>> for OwnedEvent {
    fn from(event: Event<'a>) -> Self {
        match event {
            Event::NetworkCheckSuccessful => Self::NetworkCheckSuccessful,
            Event::Success(sample) => Self::Success(sample.clone()),
            Event::Failure(error) => Self::Failure(error),
            Event::Phase(phase) => Self::Phase(phase),
        }
    }
}

impl FakeDiagnostics {
    /// Constructs a new `FakeDiagnostics`.
    pub fn new() -> Self {
        FakeDiagnostics { events: Mutex::new(Vec::new()) }
    }

    /// Assert that the recorded events equals the provided events.
    pub fn assert_events<'a, I: IntoIterator<Item = Event<'a>>>(&self, events: I) {
        let owned_events =
            events.into_iter().map(|event| OwnedEvent::from(event)).collect::<Vec<_>>();
        assert_eq!(owned_events, *self.events.lock());
    }

    /// Assert that the recorded events starts with the provided events.
    pub fn assert_events_starts_with<'a, I: IntoIterator<Item = Event<'a>>>(&self, events: I) {
        let expected_events =
            events.into_iter().map(|event| OwnedEvent::from(event)).collect::<Vec<_>>();
        let actual_events = self.events.lock();
        assert!(actual_events.len() >= expected_events.len());
        assert_eq!(expected_events.as_slice(), &actual_events[..expected_events.len()]);
    }

    /// Clears all recorded interactions.
    pub fn reset(&self) {
        self.events.lock().clear();
    }
}

impl Diagnostics for FakeDiagnostics {
    fn record<'a>(&self, event: Event<'a>) {
        self.events.lock().push(event.into());
    }
}

impl<T: AsRef<FakeDiagnostics> + Send + Sync> Diagnostics for T {
    fn record<'a>(&self, event: Event<'a>) {
        self.as_ref().record(event);
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::datatypes::Poll;
    use fuchsia_zircon as zx;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref TEST_SAMPLE: HttpsSample = HttpsSample {
            utc: zx::Time::from_nanos(111_111_111),
            monotonic: zx::Time::from_nanos(222_222_222),
            standard_deviation: zx::Duration::from_millis(235),
            final_bound_size: zx::Duration::from_millis(100),
            polls: vec![Poll { round_trip_time: zx::Duration::from_nanos(23) }],
        };
        static ref TEST_SUCCESS: Event<'static> = Event::Success(&*TEST_SAMPLE);
        static ref TEST_SAMPLE_2: HttpsSample = {
            let mut new = TEST_SAMPLE.clone();
            new.polls = vec![];
            new
        };
        static ref TEST_SUCCESS_2: Event<'static> = Event::Success(&*TEST_SAMPLE_2);
    }
    const TEST_FAILURE: Event<'static> = Event::Failure(HttpsDateErrorType::NetworkError);
    const TEST_PHASE: Event<'static> = Event::Phase(Phase::Converge);

    #[fuchsia::test]
    fn log_and_reset_events() {
        let diagnostics = FakeDiagnostics::new();
        diagnostics.assert_events(vec![]);

        diagnostics.record(*TEST_SUCCESS);
        diagnostics.assert_events(vec![*TEST_SUCCESS]);

        diagnostics.record(TEST_FAILURE);
        diagnostics.record(TEST_PHASE);
        diagnostics.assert_events(vec![*TEST_SUCCESS, TEST_FAILURE, TEST_PHASE]);

        diagnostics.reset();
        diagnostics.assert_events(vec![]);
    }

    #[test]
    #[should_panic]
    fn log_events_wrong_event_type() {
        let diagnostics = FakeDiagnostics::new();
        diagnostics.assert_events(vec![]);

        diagnostics.record(*TEST_SUCCESS);
        diagnostics.assert_events(vec![TEST_FAILURE]);
    }

    #[test]
    #[should_panic]
    fn log_events_wrong_sample() {
        let diagnostics = FakeDiagnostics::new();
        diagnostics.assert_events(vec![]);

        diagnostics.record(*TEST_SUCCESS);
        diagnostics.assert_events(vec![*TEST_SUCCESS_2]);
    }

    #[test]
    #[should_panic]
    fn log_events_wrong_event_count() {
        let diagnostics = FakeDiagnostics::new();
        diagnostics.assert_events(vec![]);

        diagnostics.record(*TEST_SUCCESS);
        diagnostics.assert_events(vec![*TEST_SUCCESS, *TEST_SUCCESS]);
    }
}
