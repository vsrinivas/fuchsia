// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for filtering events.

use fidl::encoding::Decodable;
use fidl_fuchsia_media_sessions2::*;

/// A filter accepts or rejects filter applicants.
#[derive(Debug, PartialEq)]
pub struct Filter {
    /// The options that govern which events should pass through this filter.
    options: WatchOptions,
}

impl Default for Filter {
    fn default() -> Self {
        Self { options: Decodable::new_empty() }
    }
}

/// A application to pass through a filter.
#[derive(Debug)]
pub struct FilterApplicant<T> {
    /// The options this filter applicant is known to satisfy.
    options: WatchOptions,
    /// The value that wishes to pass through the filter.
    pub applicant: T,
}

impl<T: Clone> Clone for FilterApplicant<T> {
    fn clone(&self) -> Self {
        Self {
            options: WatchOptions {
                only_active: self.options.only_active,
                allowed_sessions: self.options.allowed_sessions.clone(),
            },
            applicant: self.applicant.clone(),
        }
    }
}

impl<T> FilterApplicant<T> {
    pub fn new(options: WatchOptions, applicant: T) -> Self {
        Self { options, applicant }
    }
}

impl Filter {
    pub fn new(options: WatchOptions) -> Self {
        Self { options }
    }

    pub fn filter<T>(&self, applicant: &FilterApplicant<T>) -> bool {
        self.satisfies_filter(&applicant.options)
    }

    fn satisfies_filter(&self, options: &WatchOptions) -> bool {
        let passes_active_filter =
            !self.options.only_active.unwrap_or(false) || options.only_active.unwrap_or(false);
        let passes_allowlist_filter = self
            .options
            .allowed_sessions
            .as_ref()
            .map(|allowed_sessions| {
                options
                    .allowed_sessions
                    .as_ref()
                    .filter(|set| set.len() == 1)
                    .and_then(|ids| ids.first().copied())
                    .map(|id| allowed_sessions.contains(&id))
                    .unwrap_or(false)
            })
            .unwrap_or(true);
        passes_active_filter && passes_allowlist_filter
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn active_filter() {
        let loose_filter = Filter::new(Decodable::new_empty());
        assert_eq!(loose_filter, Filter::default());

        assert_eq!(loose_filter.filter(&FilterApplicant::new(Decodable::new_empty(), 1u32)), true);

        let active_filter =
            Filter::new(WatchOptions { only_active: Some(true), ..Decodable::new_empty() });

        assert_eq!(
            active_filter.filter(&FilterApplicant::new(Decodable::new_empty(), 1u32)),
            false
        );
        assert_eq!(
            active_filter.filter(&FilterApplicant::new(
                WatchOptions { only_active: Some(true), ..Decodable::new_empty() },
                2u32
            )),
            true
        );
    }

    #[test]
    fn allowlist_filter() {
        let loose_filter = Filter::new(Decodable::new_empty());
        assert_eq!(loose_filter, Filter::default());

        assert_eq!(loose_filter.filter(&FilterApplicant::new(Decodable::new_empty(), 1u32)), true);

        let active_filter = Filter::new(WatchOptions {
            allowed_sessions: Some(vec![0u64, 5u64]),
            ..Decodable::new_empty()
        });

        assert_eq!(
            active_filter.filter(&FilterApplicant::new(Decodable::new_empty(), 1u32)),
            false
        );

        assert_eq!(
            active_filter.filter(&FilterApplicant::new(
                WatchOptions { allowed_sessions: Some(vec![0u64]), ..Decodable::new_empty() },
                2u32
            )),
            true
        );

        assert_eq!(
            active_filter.filter(&FilterApplicant::new(
                WatchOptions { allowed_sessions: Some(vec![5u64]), ..Decodable::new_empty() },
                2u32
            )),
            true
        );

        assert_eq!(
            active_filter.filter(&FilterApplicant::new(
                WatchOptions { allowed_sessions: Some(vec![7u64]), ..Decodable::new_empty() },
                2u32
            )),
            false
        );
    }

    #[test]
    fn filter_is_intersection() {
        let loose_filter = Filter::new(Decodable::new_empty());
        assert_eq!(loose_filter, Filter::default());

        assert_eq!(loose_filter.filter(&FilterApplicant::new(Decodable::new_empty(), 1u32)), true);

        let active_filter = Filter::new(WatchOptions {
            only_active: Some(true),
            allowed_sessions: Some(vec![0u64, 5u64]),
        });

        assert_eq!(
            active_filter.filter(&FilterApplicant::new(Decodable::new_empty(), 1u32)),
            false
        );

        assert_eq!(
            active_filter.filter(&FilterApplicant::new(
                WatchOptions { only_active: Some(true), allowed_sessions: Some(vec![0u64]) },
                2u32
            )),
            true
        );

        assert_eq!(
            active_filter.filter(&FilterApplicant::new(
                WatchOptions { only_active: Some(false), allowed_sessions: Some(vec![5u64]) },
                2u32
            )),
            false
        );
    }
}
