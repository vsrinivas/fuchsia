// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod impls;
pub mod wrappers;

pub use wrappers::{InspectBytes, InspectList, InspectListClosure};

use fuchsia_inspect as finspect;

pub trait WriteInspect {
    /// Write a single value (property, metric, or object) to |node| with the specified |key|.
    /// If multiple properties or metrics need to be written, consider creating a single child
    /// node with those properties or metrics.
    fn write_inspect(&self, node: &mut finspect::ObjectTreeNode, key: &str);
}

/// Macro to log a new entry to a bounded list node with the specified key-value pairs. Each value
/// must be a type that implements `WriteInspect`. This macro automatically injects a timestamp
/// to each entry.
///
/// Example:
///
/// ```
/// let node = ...;  // wlan_inspect::nodes::BoundedListNode
/// inspect_log!(node, k1: "1", k2: 2i64, k3: "3");
/// inspect_log!(node, foo: "bar", meaning_of_life: 42u64);
/// inspect_log!(node, {
///     ba: "dum",
///     tss: "tss"
/// })
/// ```
#[macro_export]
macro_rules! inspect_log {
    // block version (allows trailing comma)
    ($bounded_list_node:expr, { $($key:ident: $val:expr,)+ }) => {
        inspect_log!($bounded_list_node, $($key: $val),+)
    };
    // block version (no-trailing comma)
    ($bounded_list_node:expr, { $($key:ident: $val:expr),+ }) => {
        inspect_log!($bounded_list_node, $($key: $val),+)
    };
    // non-block version (allows trailing comma)
    ($bounded_list_node:expr, $($key:ident: $val:expr,)+) => {
        inspect_log!($bounded_list_node, $($key: $val),+)
    };
    // non-block version (no-trailing comma)
    ($bounded_list_node:expr, $($key:ident: $val:expr),+) => {
        {
            use $crate::{log::WriteInspect, NodeExt};

            let node = $bounded_list_node.request_entry();
            let mut node = node.lock();
            node.set_time();
            $(
                $val.write_inspect(&mut node, stringify!($key));
            )+
        }
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::nodes::BoundedListNode;
    use crate::test_utils;
    use crate::NodeExt;

    use fuchsia_inspect::{self as finspect, object::ObjectUtil};

    #[test]
    fn test_inspect_log_macro() {
        let mut node = BoundedListNode::new(finspect::ObjectTreeNode::new_root(), 10);

        // Logging string and full-size numeric type
        inspect_log!(node, k1: "1".to_string(), meaning_of_life: 42u64, k3: 3i64, k4: 4f64);
        let node0 = node.inner().lock().get_child("0").expect("expect node entry 0");
        let obj0 = node0.lock().evaluate();
        obj0.get_property("@time").expect("expect time property");
        test_utils::assert_str_prop(&obj0, "k1", "1");
        test_utils::assert_uint_metric(&obj0, "meaning_of_life", 42u64);
        test_utils::assert_int_metric(&obj0, "k3", 3i64);
        test_utils::assert_double_metric(&obj0, "k4", 4f64);

        // Logging smaller numeric types (which should be converted to bigger types)
        inspect_log!(node, small_uint: 1u8, small_int: 2i8, float: 3f32);
        let node1 = node.inner().lock().get_child("1").expect("expect node entry 1");
        let obj1 = node1.lock().evaluate();
        obj1.get_property("@time").expect("expect time property");
        test_utils::assert_uint_metric(&obj1, "small_uint", 1u64);
        test_utils::assert_int_metric(&obj1, "small_int", 2i64);
        test_utils::assert_double_metric(&obj1, "float", 3f64);

        // Logging reference types + using bracket format
        inspect_log!(node, {
            s: "str",
            uint: &13u8,
        });
        let node2 = node.inner().lock().get_child("2").expect("expect node entry 2");
        let obj2 = node2.lock().evaluate();
        obj2.get_property("@time").expect("expect time property");
        test_utils::assert_str_prop(&obj2, "s", "str");
        test_utils::assert_uint_metric(&obj2, "uint", 13u64);
    }

    #[test]
    fn test_inspect_log_parsing() {
        // if this test compiles, it's considered as succeeded
        let mut node = BoundedListNode::new(finspect::ObjectTreeNode::new_root(), 10);

        // Non-block version, no trailing comma
        inspect_log!(node, k1: "v1", k2: "v2");

        // Non-block version, trailing comma
        inspect_log!(node, k1: "v1", k2: "v2",);

        // Block version, no trailing comma
        inspect_log!(node, {
            k1: "v1",
            k2: "v2"
        });

        // Block version, trailing comma
        inspect_log!(node, {
            k1: "v1",
            k2: "v2",
        });
    }

    #[test]
    fn test_inspect_log_macro_does_not_move_value() {
        // if this test compiles, it's considered as succeeded
        let mut node = BoundedListNode::new(finspect::ObjectTreeNode::new_root(), 10);
        let s = String::from("s");
        inspect_log!(node, s: s);

        // Should not cause compiler error since value is not moved
        println!("{}", s);
    }

    #[test]
    fn test_log_option() {
        let mut node = BoundedListNode::new(finspect::ObjectTreeNode::new_root(), 10);

        inspect_log!(node, some: Some("a"));
        let node0 = node.inner().lock().get_child("0").expect("expect node entry 0");
        let obj0 = node0.lock().evaluate();
        obj0.get_property("@time").expect("expect time property");
        test_utils::assert_str_prop(&obj0, "some", "a");

        inspect_log!(node, none: None as Option<String>);
        let node1 = node.inner().lock().get_child("1").expect("expect node entry 1");
        let obj1 = node1.lock().evaluate();
        obj1.get_property("@time").expect("expect time property");
        assert!(obj1.get_property("none").is_none());
    }

    #[test]
    fn test_log_inspect_bytes() {
        let mut node = BoundedListNode::new(finspect::ObjectTreeNode::new_root(), 10);
        let bytes = [11u8, 22, 33];

        inspect_log!(node, bytes: InspectBytes(&bytes));
        let node0 = node.inner().lock().get_child("0").expect("expect node entry 0");
        let obj0 = node0.lock().evaluate();
        obj0.get_property("@time").expect("expect time property");
        test_utils::assert_bytes_prop(&obj0, "bytes", vec![11, 22, 33]);
    }

    #[test]
    fn test_log_inspect_list() {
        let mut node = BoundedListNode::new(finspect::ObjectTreeNode::new_root(), 10);
        let list = [11u8, 22, 33];

        inspect_log!(node, list: InspectList(&list));
        let node0 = node.inner().lock().get_child("0").expect("expect node entry 0");
        let obj0 = node0.lock().evaluate();
        obj0.get_property("@time").expect("expect time property");

        let list_node = node0.lock().get_child("list").expect("expect node entry 'list'");
        let list_obj = list_node.lock().evaluate();
        test_utils::assert_uint_metric(&list_obj, "0", 11);
        test_utils::assert_uint_metric(&list_obj, "1", 22);
        test_utils::assert_uint_metric(&list_obj, "2", 33);
    }

    #[test]
    fn test_log_inspect_list_closure() {
        let mut node = BoundedListNode::new(finspect::ObjectTreeNode::new_root(), 10);
        let list = [13u32, 17, 29];
        let list_mapped = InspectListClosure(&list, |node, key, item| {
            node.insert(key, item * 2);
        });
        inspect_log!(node, list: list_mapped);

        let node0 = node.inner().lock().get_child("0").expect("expect node entry 0");
        let obj0 = node0.lock().evaluate();
        obj0.get_property("@time").expect("expect time property");
        let list_node = node0.lock().get_child("list").expect("expect node entry 'list'");
        let list_obj = list_node.lock().evaluate();
        test_utils::assert_uint_metric(&list_obj, "0", 26);
        test_utils::assert_uint_metric(&list_obj, "1", 34);
        test_utils::assert_uint_metric(&list_obj, "2", 58);
    }
}
