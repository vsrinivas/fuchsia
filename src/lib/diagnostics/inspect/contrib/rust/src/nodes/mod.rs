// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod list;
mod managed;

pub use list::BoundedListNode;
pub use managed::{ManagedNode, NodeWriter};

use fuchsia_inspect::Node;
use fuchsia_inspect::{Property, StringProperty};
use fuchsia_zircon as zx;

pub trait NodeExt {
    fn create_time(&self, name: impl AsRef<str>) -> TimeProperty;
    fn create_time_at(&self, name: impl AsRef<str>, timestamp: zx::Time) -> TimeProperty;
}

impl NodeExt for Node {
    fn create_time(&self, name: impl AsRef<str>) -> TimeProperty {
        let now = zx::Time::get(zx::ClockId::Monotonic);
        self.create_time_at(name, now)
    }

    fn create_time_at(&self, name: impl AsRef<str>, timestamp: zx::Time) -> TimeProperty {
        TimeProperty { inner: self.create_string(name, &format_time(timestamp)) }
    }
}

pub struct TimeProperty {
    // TODO(fxbug.dev/29628) - if we have something to post-process Inspect JSON dump, it would be
    //                   better to log timestamp as Uint.
    pub(crate) inner: StringProperty,
}

impl TimeProperty {
    pub fn update(&self) {
        let now = zx::Time::get(zx::ClockId::Monotonic);
        self.set_at(now);
    }

    pub fn set_at(&self, timestamp: zx::Time) {
        Property::set(&self.inner, &format_time(timestamp));
    }
}

fn format_time(timestamp: zx::Time) -> String {
    let seconds = timestamp.into_nanos() / 1000_000_000;
    let millis = (timestamp.into_nanos() % 1000_000_000) / 1000_000;
    format!("{}.{:03}", seconds, millis)
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
        assert_inspect_tree!(inspector, root: { time: "123.456" });
        time_property.set_at(zx::Time::from_nanos(333_005000000));
        assert_inspect_tree!(inspector, root: { time: "333.005" });
        time_property.set_at(zx::Time::from_nanos(333_444000000));
        assert_inspect_tree!(inspector, root: { time: "333.444" });
    }
}
