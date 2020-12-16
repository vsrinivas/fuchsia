// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities and wrappers providing higher level functionality for Inspect Nodes and properties.

mod list;
mod managed;

pub use list::BoundedListNode;
pub use managed::{ManagedNode, NodeWriter};

use fuchsia_inspect::Node;
use fuchsia_inspect::{IntProperty, Property};
use fuchsia_zircon as zx;

/// Extension trait that allows to manage timestamp properties.
pub trait NodeExt {
    /// Creates a new property holding the current monotonic timestamp.
    fn create_time(&self, name: impl AsRef<str>) -> TimeProperty;

    /// Creates a new property holding the given timestamp.
    fn create_time_at(&self, name: impl AsRef<str>, timestamp: zx::Time) -> TimeProperty;
}

impl NodeExt for Node {
    fn create_time(&self, name: impl AsRef<str>) -> TimeProperty {
        let now = zx::Time::get_monotonic();
        self.create_time_at(name, now)
    }

    fn create_time_at(&self, name: impl AsRef<str>, timestamp: zx::Time) -> TimeProperty {
        TimeProperty { inner: self.create_int(name, timestamp.into_nanos()) }
    }
}

/// Wrapper around an int property that stores a monotonic timestamp.
pub struct TimeProperty {
    pub(crate) inner: IntProperty,
}

impl TimeProperty {
    /// Updates the underlying property with the current monotonic timestamp.
    pub fn update(&self) {
        let now = zx::Time::get_monotonic();
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

    use fuchsia_inspect::{assert_inspect_tree, Inspector};

    #[test]
    fn test_time_metadata_format() {
        let inspector = Inspector::new();
        let time_property =
            inspector.root().create_time_at("time", zx::Time::from_nanos(123_456700000));
        assert_inspect_tree!(inspector, root: { time: 123_456700000i64 });
        time_property.set_at(zx::Time::from_nanos(333_005000000));
        assert_inspect_tree!(inspector, root: { time: 333_005000000i64 });
        time_property.set_at(zx::Time::from_nanos(333_444000000));
        assert_inspect_tree!(inspector, root: { time: 333_444000000i64 });
    }
}
