// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::WriteInspect;

use crate::NodeExt;

use fidl_fuchsia_inspect as fidl_inspect;
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use fuchsia_inspect as finspect;

// --- Utility macros to help with implementing WriteInspect ---

macro_rules! impl_write_inspect {
    ($inspect_value_type:ident, $_self:ident => $self_expr:expr, $($ty:ty),+) => {
        $(
            impl WriteInspect for $ty {
                fn write_inspect(&$_self, node: &mut finspect::ObjectTreeNode, key: &str) {
                    write_inspect_value!($inspect_value_type, node, key, $self_expr);
                }
            }
        )+
    }
}

macro_rules! write_inspect_value {
    (Str, $node:expr, $key:expr, $expr:expr) => {
        $node.insert_str($key, $expr);
    };
    (Bytes, $node:expr, $key:expr, $expr:expr) => {
        $node.add_property(fidl_inspect::Property {
            key: $key.to_string(),
            value: fidl_inspect::PropertyValue::Bytes($expr),
        });
    };
    (Uint, $node:expr, $key:expr, $expr:expr) => {
        $node.add_metric(fidl_inspect::Metric {
            key: $key.to_string(),
            value: fidl_inspect::MetricValue::UintValue($expr),
        });
    };
    (Int, $node:expr, $key:expr, $expr:expr) => {
        $node.add_metric(fidl_inspect::Metric {
            key: $key.to_string(),
            value: fidl_inspect::MetricValue::IntValue($expr),
        });
    };
    (Double, $node:expr, $key:expr, $expr:expr) => {
        $node.add_metric(fidl_inspect::Metric {
            key: $key.to_string(),
            value: fidl_inspect::MetricValue::DoubleValue($expr),
        });
    };
}

// --- Implementations for basic types ---

impl_write_inspect!(Str, self => self.to_string(), &str, String);
impl_write_inspect!(Uint, self => (*self).into(), u8, u16, u32, u64);
impl_write_inspect!(Int, self => (*self).into(), i8, i16, i32, i64);
impl_write_inspect!(Double, self => (*self).into(), f32, f64);

impl<V: WriteInspect + ?Sized> WriteInspect for &V {
    fn write_inspect(&self, node: &mut finspect::ObjectTreeNode, key: &str) {
        (*self).write_inspect(node, key);
    }
}

// --- Implementations for WLAN types ---

impl_write_inspect!(Str, self => format!("{:?}", self), fidl_common::Cbw, fidl_mlme::BssTypes);

impl WriteInspect for fidl_common::WlanChan {
    fn write_inspect(&self, node: &mut finspect::ObjectTreeNode, key: &str) {
        node.create_child(key)
            .lock()
            .insert("primary", self.primary)
            .insert("cbw", self.cbw)
            .insert("secondary80", self.secondary80);
    }
}
