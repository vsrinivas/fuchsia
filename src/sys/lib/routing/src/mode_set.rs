// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{error::EventsRoutingError, walk_state::WalkStateUnit},
    cm_rust::EventMode,
};

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct EventModeSet {
    supports_sync: bool,
}

impl EventModeSet {
    pub fn new(mode: cm_rust::EventMode) -> Self {
        Self { supports_sync: mode == cm_rust::EventMode::Sync }
    }
    pub fn debug() -> Self {
        Self { supports_sync: true }
    }

    pub fn supports_mode(&self, mode: &EventMode) -> bool {
        match mode {
            EventMode::Sync => self.supports_sync,
            EventMode::Async => true,
        }
    }
}

impl WalkStateUnit for EventModeSet {
    type Error = EventsRoutingError;

    /// Ensures the next walk state of filters is a superset of the current state.
    ///
    /// Consider A->B where A (next_state) is offering an event to B (self) and B is using it itself
    /// or offering it again.
    ///
    /// For all properties of B, those properties are in A and they are subsets of the property in
    /// B.
    fn validate_next(&self, next_state: &EventModeSet) -> Result<(), Self::Error> {
        match (self.supports_sync, next_state.supports_sync) {
            (_, true) => Ok(()),
            (false, false) => Ok(()),
            (true, false) => {
                Err(EventsRoutingError::CannotPropagateEventMode { event_mode: EventMode::Sync })
            }
        }
    }

    fn finalize_error() -> Self::Error {
        EventsRoutingError::MissingModes
    }
}

#[cfg(test)]
mod tests {
    use {super::*, cm_rust::EventMode, matches::assert_matches};

    #[test]
    fn test_walk_state() {
        let async_filter = EventModeSet::new(EventMode::Async);
        let sync_filter = EventModeSet::new(EventMode::Sync);

        assert_matches!(async_filter.validate_next(&async_filter), Ok(()));

        assert_matches!(
            sync_filter.validate_next(&async_filter),
            Err(EventsRoutingError::CannotPropagateEventMode { event_mode: EventMode::Sync })
        );
        assert_matches!(async_filter.validate_next(&sync_filter), Ok(()));
        assert_matches!(async_filter.validate_next(&async_filter), Ok(()));
    }

    #[test]
    fn contains() {
        let async_filter = EventModeSet::new(EventMode::Async);
        let sync_filter = EventModeSet::new(EventMode::Sync);
        assert!(async_filter.supports_mode(&EventMode::Async));
        assert!(sync_filter.supports_mode(&EventMode::Sync));
        assert!(!async_filter.supports_mode(&EventMode::Sync));
        assert!(sync_filter.supports_mode(&EventMode::Sync));
    }
}
