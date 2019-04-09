// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod iface_mgr;
pub mod nodes;

#[cfg(test)]
mod test_utils;

pub use iface_mgr::IfaceManager;
pub use nodes::{NodeExt, SharedNodePtr};

use fidl_fuchsia_inspect as fidl_inspect;

/// A wrapper to abstract away the types of inspect values: property, metric, and object.
///
/// Note that since an object type is technically a key-value pair (since object's name can
/// be thought of as a key), in the context of `InspectValue`, the object's name is ignored.
pub enum InspectValue {
    Property(fidl_inspect::PropertyValue),
    Metric(fidl_inspect::MetricValue),
    Object(fidl_inspect::Object),
}

pub trait ToInspectValue {
    fn to_inspect_value(&self) -> InspectValue;
}

macro_rules! impl_to_inspect_value {
    ($inspect_value_type:ident, $($ty:ty),+) => {
        $(
            impl ToInspectValue for $ty {
                fn to_inspect_value(&self) -> InspectValue {
                    make_inspect_value!($inspect_value_type, self)
                }
            }
        )+
    }
}

macro_rules! make_inspect_value {
    (Str, $_self:expr) => {
        InspectValue::Property(fidl_inspect::PropertyValue::Str($_self.to_string()))
    };
    (Bytes, $_self:expr) => {
        InspectValue::Property(fidl_inspect::PropertyValue::Bytes($_self.to_vec()))
    };
    (Uint, $_self:expr) => {
        InspectValue::Metric(fidl_inspect::MetricValue::UintValue((*$_self).into()))
    };
    (Int, $_self:expr) => {
        InspectValue::Metric(fidl_inspect::MetricValue::IntValue((*$_self).into()))
    };
    (Double, $_self:expr) => {
        InspectValue::Metric(fidl_inspect::MetricValue::DoubleValue((*$_self).into()))
    };
}

macro_rules! impl_to_inspect_value_fixed_arrays {
    ($($N:expr)+) => {
        impl_to_inspect_value!(Bytes, $([u8; $N]),+);
    }
}

impl_to_inspect_value!(Str, &str, String);
impl_to_inspect_value!(Bytes, Vec<u8>, [u8]);
impl_to_inspect_value_fixed_arrays!(
    0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19
    20 21 22 23 24 25 26 27 28 29 30 31 32
);
impl_to_inspect_value!(Uint, u8, u16, u32, u64);
impl_to_inspect_value!(Int, i8, i16, i32, i64);
impl_to_inspect_value!(Double, f32, f64);

impl<V: ToInspectValue + ?Sized> ToInspectValue for &V {
    fn to_inspect_value(&self) -> InspectValue {
        (*self).to_inspect_value()
    }
}

/// Macro to log a new entry to a bounded list node with the specified key-value pairs. Each value
/// must be a type that implements `ToInspectValue`. This macro automatically injects a timestamp
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
///
/// The following keys are reserved and cannot be specified: `time`
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
        $bounded_list_node.request_entry().lock().set_time()
        $(
            .insert(stringify!($key), &$val)
        )+
        ;
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::nodes::BoundedListNode;
    use crate::test_utils;

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

        // Logging types that convert to bytes
        inspect_log!(node, vec: vec![1u8, 2, 3], array: [4u8, 5, 6]);
        let node2 = node.inner().lock().get_child("2").expect("expect node entry 2");
        let obj2 = node2.lock().evaluate();
        obj2.get_property("@time").expect("expect time property");
        test_utils::assert_bytes_prop(&obj2, "vec", vec![1, 2, 3]);
        test_utils::assert_bytes_prop(&obj2, "array", vec![4, 5, 6]);

        // Logging reference types + using bracket format
        inspect_log!(node, {
            s: "str",
            uint: &13u8,
            vecref: &vec![1u8],
            slice: &[2u8][..],
            fixedslice: &[3u8]
        });
        let node3 = node.inner().lock().get_child("3").expect("expect node entry 3");
        let obj3 = node3.lock().evaluate();
        obj3.get_property("@time").expect("expect time property");
        test_utils::assert_str_prop(&obj3, "s", "str");
        test_utils::assert_uint_metric(&obj3, "uint", 13u64);
        test_utils::assert_bytes_prop(&obj3, "vecref", vec![1]);
        test_utils::assert_bytes_prop(&obj3, "slice", vec![2]);
        test_utils::assert_bytes_prop(&obj3, "fixedslice", vec![3]);
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
}
