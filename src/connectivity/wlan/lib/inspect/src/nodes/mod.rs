// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod list;

pub use list::BoundedListNode;

use crate::log::WriteInspect;

use fidl_fuchsia_inspect as fidl_inspect;
use fuchsia_inspect::{self as finspect, object::ObjectUtil};
use fuchsia_zircon as zx;
use parking_lot::Mutex;
use std::sync::Arc;

pub type SharedNodePtr = Arc<Mutex<finspect::ObjectTreeNode>>;

pub trait NodeExt {
    /// Create and add child to |&self| and return it.
    /// This differs from `add_child` which returns previously existing child node with same name.
    /// For this function, previously existing child node is discarded.
    fn create_child(&mut self, child_name: &str) -> SharedNodePtr;

    fn set_time(&mut self) -> &mut Self;
    fn set_time_at(&mut self, timestamp: zx::Time) -> &mut Self;
    fn insert_str<S: Into<String>>(&mut self, key: &str, value: S) -> &mut Self;
    fn insert_debug<D: std::fmt::Debug>(&mut self, key: &str, value: D) -> &mut Self;
    fn insert<V: WriteInspect>(&mut self, key: &str, value: V) -> &mut Self;
    fn insert_maybe<V: WriteInspect>(&mut self, key: &str, value: Option<V>) -> &mut Self;
}

impl NodeExt for finspect::ObjectTreeNode {
    fn create_child(&mut self, child_name: &str) -> SharedNodePtr {
        let child =
            finspect::ObjectTreeNode::new(fidl_inspect::Object::new(child_name.to_string()));
        let _prev_child = self.add_child_tree(child.clone());
        child
    }

    fn set_time(&mut self) -> &mut Self {
        let now = zx::Time::get(zx::ClockId::Monotonic);
        self.set_time_at(now)
    }

    fn set_time_at(&mut self, timestamp: zx::Time) -> &mut Self {
        // TODO(WLAN-1010) - if we have something to post-process Inspect JSON dump, it would be
        //                   better to log the timestamp as MetricValue::UintValue.
        let seconds = timestamp.into_nanos() / 1000_000_000;
        let millis = (timestamp.into_nanos() % 1000_000_000) / 1000_000;
        self.add_property(fidl_inspect::Property {
            key: "@time".to_string(),
            value: fidl_inspect::PropertyValue::Str(format!("{}.{}", seconds, millis)),
        });
        self
    }

    fn insert_str<S: Into<String>>(&mut self, key: &str, value: S) -> &mut Self {
        self.add_property(fidl_inspect::Property {
            key: key.to_string(),
            value: fidl_inspect::PropertyValue::Str(value.into()),
        });
        self
    }

    fn insert_debug<D: std::fmt::Debug>(&mut self, key: &str, value: D) -> &mut Self {
        self.insert_str(key, format!("{:?}", value))
    }

    fn insert<V: WriteInspect>(&mut self, key: &str, value: V) -> &mut Self {
        value.write_inspect(self, key);
        self
    }

    fn insert_maybe<V: WriteInspect>(&mut self, key: &str, value: Option<V>) -> &mut Self {
        if let Some(ref value) = value {
            value.write_inspect(self, key);
        }
        self
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::test_utils;
    use fuchsia_inspect as finspect;

    #[test]
    fn test_time_metadata_format() {
        let node = finspect::ObjectTreeNode::new_root();
        let timestamp = zx::Time::from_nanos(123_456700000);
        node.lock().set_time_at(timestamp);
        let object = node.lock().evaluate();
        test_utils::assert_str_prop(&object, "@time", "123.456");
    }
}
