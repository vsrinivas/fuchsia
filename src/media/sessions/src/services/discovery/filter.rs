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
            options: WatchOptions { only_active: self.options.only_active },
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
        !self.options.only_active.unwrap_or(false) || options.only_active.unwrap_or(false)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn it_works() {
        let loose_filter = Filter::new(Decodable::new_empty());
        assert_eq!(loose_filter, Filter::default());

        assert_eq!(loose_filter.filter(&FilterApplicant::new(Decodable::new_empty(), 1u32)), true);

        let active_filter = Filter::new(WatchOptions { only_active: Some(true) });

        assert_eq!(
            active_filter.filter(&FilterApplicant::new(Decodable::new_empty(), 1u32)),
            false
        );
        assert_eq!(
            active_filter
                .filter(&FilterApplicant::new(WatchOptions { only_active: Some(true) }, 2u32)),
            true
        );
    }
}
