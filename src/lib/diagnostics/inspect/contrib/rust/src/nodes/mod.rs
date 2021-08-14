// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities and wrappers providing higher level functionality for Inspect Nodes and properties.

mod list;

pub use list::BoundedListNode;

use fuchsia_inspect::Node;
use fuchsia_inspect::{IntProperty, Property, StringReference};
use fuchsia_zircon as zx;

/// Extension trait that allows to manage timestamp properties.
///
/// [`NodeExt::create_time`] and [`NodeExt::record_time`] require the caller to have a
/// fuchsia_async executor set up, otherwise they will panic.
pub trait NodeExt {
    /// Creates a new property holding the current monotonic timestamp.
    fn create_time<'b>(&self, name: impl Into<StringReference<'b>>) -> TimeProperty;

    /// Creates a new property holding the given timestamp.
    fn create_time_at<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        timestamp: zx::Time,
    ) -> TimeProperty;

    /// Records a new property holding the current monotonic timestamp.
    fn record_time<'b>(&self, name: impl Into<StringReference<'b>>);
}

impl NodeExt for Node {
    ///# Panics
    ///
    /// Panics if the caller did not set up a fuchsia_async executor
    fn create_time<'b>(&self, name: impl Into<StringReference<'b>>) -> TimeProperty {
        let now = fuchsia_async::Time::now().into();
        self.create_time_at(name, now)
    }

    fn create_time_at<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        timestamp: zx::Time,
    ) -> TimeProperty {
        TimeProperty { inner: self.create_int(name, timestamp.into_nanos()) }
    }

    ///# Panics
    ///
    /// Panics if the caller did not set up a fuchsia_async executor
    fn record_time<'b>(&self, name: impl Into<StringReference<'b>>) {
        let now: zx::Time = fuchsia_async::Time::now().into();
        self.record_int(name, now.into_nanos());
    }
}

/// Wrapper around an int property that stores a monotonic timestamp.
///
/// [`TimeProperty::update`] requires the caller to have a fuchsia_async executor set up, otherwise
/// it will panic.
pub struct TimeProperty {
    pub(crate) inner: IntProperty,
}

impl TimeProperty {
    /// Updates the underlying property with the current monotonic timestamp.
    ///
    ///# Panics
    ///
    /// Panics if the caller did not set up a fuchsia_async executor
    pub fn update(&self) {
        let now = fuchsia_async::Time::now().into();
        self.set_at(now);
    }

    /// Updates the underlying property with the given timestamp.
    pub fn set_at(&self, timestamp: zx::Time) {
        Property::set(&self.inner, timestamp.into_nanos());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async as fasync;
    use fuchsia_inspect::{assert_data_tree, Inspector};

    #[test]
    fn test_time_metadata_format() {
        let inspector = Inspector::new();
        let time_property =
            inspector.root().create_time_at("time", zx::Time::from_nanos(123_456700000));
        assert_data_tree!(inspector, root: { time: 123_456700000i64 });
        time_property.set_at(zx::Time::from_nanos(333_005000000));
        assert_data_tree!(inspector, root: { time: 333_005000000i64 });
        time_property.set_at(zx::Time::from_nanos(333_444000000));
        assert_data_tree!(inspector, root: { time: 333_444000000i64 });
    }

    #[test]
    fn test_create_time_and_update() {
        let executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fasync::Time::from_nanos(0));
        let inspector = Inspector::new();
        let time_property = inspector.root().create_time("time");
        assert_data_tree!(inspector, root: { time: 0i64 });
        executor.set_fake_time(fasync::Time::from_nanos(5));
        time_property.update();
        assert_data_tree!(inspector, root: { time: 5i64 });
        executor.set_fake_time(fasync::Time::from_nanos(10));
        time_property.update();
        assert_data_tree!(inspector, root: { time: 10i64 });
    }

    #[test]
    fn test_record_time() {
        let executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fasync::Time::from_nanos(55));
        let inspector = Inspector::new();
        inspector.root().record_time("time");
        assert_data_tree!(inspector, root: { time: 55i64 });
    }

    #[test]
    #[should_panic(expected = "Fuchsia Executor must be created first")]
    fn test_create_time_no_executor() {
        let inspector = Inspector::new();
        inspector.root().create_time("time");
    }

    #[test]
    #[should_panic(expected = "Fuchsia Executor must be created first")]
    fn test_record_time_no_executor() {
        let inspector = Inspector::new();
        inspector.root().record_time("time");
    }
}
