// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, Event},
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

    /// Panics if the supplied slice does not match the received events.
    pub fn assert_events(&self, expected: &[Event]) {
        assert_eq!(*self.events.lock(), expected);
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

#[cfg(test)]
mod test {
    use {super::*, crate::enums::InitialClockState};

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
}
