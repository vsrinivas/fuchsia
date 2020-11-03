// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, Event, ANY_DURATION, ANY_TIME},
    fuchsia_zircon as zx,
    parking_lot::Mutex,
};

/// A fake `Diagnostics` implementation useful for verifying unittest.
pub struct FakeDiagnostics {
    /// An ordered list of the events received since the last reset.
    events: Mutex<Vec<Event>>,
}

impl FakeDiagnostics {
    /// Constructs a new `FakeDiagnostics`.
    pub fn new() -> Self {
        FakeDiagnostics { events: Mutex::new(Vec::new()) }
    }

    /// Panics if the supplied slice does not match the received events. When present in
    /// expected, the special values ANY_TIME and ANY_DURATION will match any received value.
    pub fn assert_events(&self, expected: &[Event]) {
        let events_lock = self.events.lock();
        if !expected.eq_with_any(&events_lock) {
            // If we failed to match considering sentinels we are guaranteed to fail without
            // considering them; use the standard assert_eq to generate a nicely formatted error.
            assert_eq!(*events_lock, expected);
        }
    }

    /// Clears all recorded interactions.
    pub fn reset(&self) {
        self.events.lock().clear();
    }
}

impl Diagnostics for FakeDiagnostics {
    fn record(&self, event: Event) {
        self.events.lock().push(event);
    }
}

impl<T: AsRef<FakeDiagnostics> + Send + Sync> Diagnostics for T {
    fn record(&self, event: Event) {
        self.as_ref().events.lock().push(event);
    }
}

trait EqWithAny {
    /// Tests `self` and `other` for equality equal, treating any special "any" sentinel values in
    /// `self` as matching any value in `other`.
    fn eq_with_any(&self, other: &Self) -> bool;
}

impl EqWithAny for zx::Duration {
    fn eq_with_any(&self, other: &Self) -> bool {
        *self == ANY_DURATION || self == other
    }
}

impl EqWithAny for zx::Time {
    fn eq_with_any(&self, other: &Self) -> bool {
        *self == *ANY_TIME || self == other
    }
}

impl<T: EqWithAny> EqWithAny for Option<T> {
    fn eq_with_any(&self, other: &Self) -> bool {
        match (self, other) {
            (None, None) => true,
            (Some(self_t), Some(other_t)) => self_t.eq_with_any(other_t),
            _ => false,
        }
    }
}

impl<T: EqWithAny> EqWithAny for [T] {
    fn eq_with_any(&self, other: &[T]) -> bool {
        if self.len() != other.len() {
            return false;
        }
        self.iter()
            .zip(other.iter())
            .all(|(self_entry, other_entry)| self_entry.eq_with_any(other_entry))
    }
}

impl EqWithAny for Event {
    fn eq_with_any(&self, other: &Event) -> bool {
        match self {
            Event::InitializeRtc { outcome, time } => match other {
                Event::InitializeRtc { outcome: other_outcome, time: other_time } => {
                    outcome == other_outcome && time.eq_with_any(other_time)
                }
                _ => false,
            },
            Event::EstimateUpdated { track, offset, sqrt_covariance } => match other {
                Event::EstimateUpdated {
                    track: other_track,
                    offset: other_offset,
                    sqrt_covariance: other_sqrt_cov,
                } => {
                    track == other_track
                        && offset.eq_with_any(other_offset)
                        && sqrt_covariance.eq_with_any(other_sqrt_cov)
                }
                _ => false,
            },
            Event::ClockCorrection { track, correction, strategy } => match other {
                Event::ClockCorrection {
                    track: other_track,
                    correction: other_correction,
                    strategy: other_strategy,
                } => {
                    track == other_track
                        && correction.eq_with_any(other_correction)
                        && strategy == other_strategy
                }
                _ => false,
            },
            _ => self.eq(other),
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::enums::{InitialClockState, Track},
    };

    const INITIALIZATION_EVENT: Event =
        Event::Initialized { clock_state: InitialClockState::NotSet };

    const NETWORK_EVENT: Event = Event::NetworkAvailable;

    #[test]
    fn log_and_reset_events() {
        let diagnostics = FakeDiagnostics::new();
        diagnostics.assert_events(&[]);

        diagnostics.record(INITIALIZATION_EVENT);
        diagnostics.assert_events(&[INITIALIZATION_EVENT]);

        diagnostics.record(NETWORK_EVENT);
        diagnostics.assert_events(&[INITIALIZATION_EVENT, NETWORK_EVENT]);

        diagnostics.reset();
        diagnostics.assert_events(&[]);

        diagnostics.record(NETWORK_EVENT);
        diagnostics.assert_events(&[NETWORK_EVENT]);
    }

    #[test]
    fn match_wildcards() {
        let diagnostics = FakeDiagnostics::new();
        let test_event = Event::EstimateUpdated {
            track: Track::Monitor,
            offset: zx::Duration::from_seconds(1234),
            sqrt_covariance: zx::Duration::from_millis(321),
        };

        diagnostics.record(test_event.clone());
        diagnostics.assert_events(&[test_event]);

        diagnostics.assert_events(&[Event::EstimateUpdated {
            track: Track::Monitor,
            offset: ANY_DURATION,
            sqrt_covariance: zx::Duration::from_millis(321),
        }]);

        diagnostics.assert_events(&[Event::EstimateUpdated {
            track: Track::Monitor,
            offset: zx::Duration::from_seconds(1234),
            sqrt_covariance: ANY_DURATION,
        }]);

        diagnostics.assert_events(&[Event::EstimateUpdated {
            track: Track::Monitor,
            offset: ANY_DURATION,
            sqrt_covariance: ANY_DURATION,
        }]);
    }
}
