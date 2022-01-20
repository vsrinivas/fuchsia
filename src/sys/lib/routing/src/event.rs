// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{error::EventsRoutingError, walk_state::WalkStateUnit},
    cm_rust::{CapabilityName, DictionaryValue, EventMode},
    maplit::hashmap,
    std::collections::HashMap,
};

#[derive(Debug)]
pub struct EventSubscription {
    pub event_name: CapabilityName,
    /// Determines whether component manager waits for a response from the
    /// event receiver.
    pub mode: EventMode,
}

impl EventSubscription {
    pub fn new(event_name: CapabilityName, mode: EventMode) -> Self {
        Self { event_name, mode }
    }
}

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

type OptionFilterMap = Option<HashMap<String, DictionaryValue>>;

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct EventFilter {
    filter: Option<HashMap<String, DictionaryValue>>,
    is_debug: bool,
}

impl EventFilter {
    pub fn new(filter: Option<HashMap<String, DictionaryValue>>) -> Self {
        Self { filter, is_debug: false }
    }

    pub fn debug() -> Self {
        Self { filter: None, is_debug: true }
    }

    /// Verifies that for all fields given, they are present in the current filter. If no fields
    /// are given, returns true.
    pub fn has_fields(&self, fields: &OptionFilterMap) -> bool {
        if self.is_debug {
            return true;
        }
        Self::validate_subset(&fields, &self.filter).is_ok()
    }

    pub fn contains(&self, key: impl Into<String>, values: Vec<String>) -> bool {
        self.has_fields(&Some(hashmap! {key.into() => DictionaryValue::StrVec(values)}))
    }

    fn validate_subset(
        self_filter: &OptionFilterMap,
        next_filter: &OptionFilterMap,
    ) -> Result<(), EventsRoutingError> {
        match (self_filter, next_filter) {
            (None, None) => {}
            (None, Some(_)) => {}
            (Some(filter), Some(next_filter)) => {
                for (key, value) in filter {
                    if !(next_filter.contains_key(key)
                        && is_subset(value, next_filter.get(key).as_ref().unwrap()))
                    {
                        return Err(EventsRoutingError::InvalidFilter);
                    }
                }
            }
            (Some(_), None) => {
                return Err(EventsRoutingError::InvalidFilter);
            }
        }
        Ok(())
    }
}

impl WalkStateUnit for EventFilter {
    type Error = EventsRoutingError;

    /// Ensures the next walk state of filters is a superset of the current state.
    ///
    /// Consider A->B where A (next_state) is offering an event to B (self) and B is using it itself
    /// or offering it again.
    ///
    /// For all properties of B, those properties are in A and they are subsets of the property in
    /// B.
    fn validate_next(&self, next_state: &EventFilter) -> Result<(), Self::Error> {
        Self::validate_subset(&self.filter, &next_state.filter)
    }

    fn finalize_error() -> Self::Error {
        EventsRoutingError::MissingFilter
    }
}

fn is_subset(prev_value: &DictionaryValue, next_value: &DictionaryValue) -> bool {
    match (prev_value, next_value) {
        (DictionaryValue::Str(field), DictionaryValue::Str(next_field)) => field == next_field,
        (DictionaryValue::StrVec(fields), DictionaryValue::StrVec(next_fields)) => {
            fields.iter().all(|field| next_fields.contains(field))
        }
        (DictionaryValue::Str(field), DictionaryValue::StrVec(next_fields)) => {
            next_fields.contains(field)
        }
        (DictionaryValue::StrVec(fields), DictionaryValue::Str(next_field)) => {
            if fields.is_empty() {
                return true;
            }
            if fields.len() > 1 {
                return false;
            }
            fields.contains(next_field)
        }
        // Self is a vector, next is a unit. Not subset.
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, cm_rust::EventMode, maplit::hashmap};

    #[test]
    fn test_mode_set_walk_state() {
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
    fn contains_mode_set() {
        let async_filter = EventModeSet::new(EventMode::Async);
        let sync_filter = EventModeSet::new(EventMode::Sync);
        assert!(async_filter.supports_mode(&EventMode::Async));
        assert!(sync_filter.supports_mode(&EventMode::Sync));
        assert!(!async_filter.supports_mode(&EventMode::Sync));
        assert!(sync_filter.supports_mode(&EventMode::Sync));
    }

    #[test]
    fn test_filter_walk_state() {
        let none_filter = EventFilter::new(None);
        let empty_filter = EventFilter::new(Some(hashmap! {}));
        let single_field_filter = EventFilter::new(Some(hashmap! {
            "field".to_string() => DictionaryValue::Str("/foo".to_string()),
        }));
        let single_field_filter_2 = EventFilter::new(Some(hashmap! {
            "field".to_string() => DictionaryValue::Str("/bar".to_string()),
        }));
        let multi_field_filter = EventFilter::new(Some(hashmap! {
            "field".to_string() => DictionaryValue::StrVec(vec![
                                    "/bar".to_string(), "/baz".to_string()])
        }));
        let multi_field_filter_2 = EventFilter::new(Some(hashmap! {
            "field".to_string() => DictionaryValue::StrVec(vec![
                                    "/bar".to_string(), "/baz".to_string(), "/foo".to_string()])
        }));
        let multi_field_single = EventFilter::new(Some(hashmap! {
            "field".to_string() => DictionaryValue::StrVec(vec!["/foo".to_string()])
        }));
        let multi_field_empty = EventFilter::new(Some(hashmap! {
            "field".to_string() => DictionaryValue::StrVec(vec![])
        }));

        assert_matches!(none_filter.validate_next(&none_filter), Ok(()));

        assert_matches!(
            single_field_filter.validate_next(&none_filter),
            Err(EventsRoutingError::InvalidFilter)
        );
        assert_matches!(
            single_field_filter.validate_next(&empty_filter),
            Err(EventsRoutingError::InvalidFilter)
        );
        assert_matches!(single_field_filter.validate_next(&single_field_filter), Ok(()));
        assert_matches!(
            single_field_filter.validate_next(&single_field_filter_2),
            Err(EventsRoutingError::InvalidFilter)
        );
        assert_matches!(
            single_field_filter.validate_next(&multi_field_filter),
            Err(EventsRoutingError::InvalidFilter)
        );
        assert_matches!(single_field_filter.validate_next(&multi_field_filter_2), Ok(()));

        assert_matches!(
            multi_field_filter.validate_next(&none_filter),
            Err(EventsRoutingError::InvalidFilter)
        );
        assert_matches!(
            multi_field_filter_2.validate_next(&multi_field_filter),
            Err(EventsRoutingError::InvalidFilter)
        );
        assert_matches!(
            multi_field_filter.validate_next(&single_field_filter),
            Err(EventsRoutingError::InvalidFilter)
        );
        assert_matches!(
            multi_field_filter.validate_next(&single_field_filter_2),
            Err(EventsRoutingError::InvalidFilter)
        );
        assert_matches!(multi_field_filter.validate_next(&multi_field_filter), Ok(()));
        assert_matches!(multi_field_filter.validate_next(&multi_field_filter_2), Ok(()));
        assert_matches!(
            multi_field_filter.validate_next(&empty_filter),
            Err(EventsRoutingError::InvalidFilter)
        );

        assert_matches!(
            empty_filter.validate_next(&none_filter),
            Err(EventsRoutingError::InvalidFilter)
        );
        assert_matches!(empty_filter.validate_next(&empty_filter), Ok(()));
        assert_matches!(empty_filter.validate_next(&single_field_filter), Ok(()));
        assert_matches!(empty_filter.validate_next(&multi_field_filter), Ok(()));

        assert_matches!(multi_field_single.validate_next(&single_field_filter), Ok(()));
        assert_matches!(multi_field_empty.validate_next(&single_field_filter), Ok(()));
    }

    #[test]
    fn contains_filter() {
        let filter = EventFilter::new(Some(hashmap! {
            "field".to_string() => DictionaryValue::StrVec(vec!["/foo".to_string(), "/bar".to_string()]),
        }));

        assert!(filter.contains("field", vec!["/foo".to_string()]));
        assert!(filter.contains("field", vec!["/foo".to_string(), "/bar".to_string()]));
        assert!(!filter.contains("field2", vec!["/foo".to_string()]));
        assert!(!filter
            .contains("field2", vec!["/foo".to_string(), "/bar".to_string(), "/baz".to_string()]));
        assert!(!filter.contains("field2", vec!["/baz".to_string()]));
    }
}
