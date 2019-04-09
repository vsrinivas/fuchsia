// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_inspect as fidl_inspect;
use fuchsia_inspect::object::ObjectUtil;

pub fn assert_str_prop(object: &fidl_inspect::Object, key: &str, val: &str) {
    assert_prop(object, key, fidl_inspect::PropertyValue::Str(val.to_string()));
}

pub fn assert_bytes_prop(object: &fidl_inspect::Object, key: &str, val: Vec<u8>) {
    assert_prop(object, key, fidl_inspect::PropertyValue::Bytes(val));
}

pub fn assert_prop(object: &fidl_inspect::Object, key: &str, value: fidl_inspect::PropertyValue) {
    let property = object.get_property(key).expect(&format!("expect {} property", key));
    assert_eq!(property, &fidl_inspect::Property { key: key.to_string(), value });
}

pub fn assert_uint_metric(object: &fidl_inspect::Object, key: &str, val: u64) {
    assert_metric(object, key, fidl_inspect::MetricValue::UintValue(val));
}

pub fn assert_int_metric(object: &fidl_inspect::Object, key: &str, val: i64) {
    assert_metric(object, key, fidl_inspect::MetricValue::IntValue(val));
}

pub fn assert_double_metric(object: &fidl_inspect::Object, key: &str, val: f64) {
    assert_metric(object, key, fidl_inspect::MetricValue::DoubleValue(val));
}

pub fn assert_metric(object: &fidl_inspect::Object, key: &str, value: fidl_inspect::MetricValue) {
    let metric = object.get_metric(key).expect(&format!("expect {} metric", key));
    assert_eq!(metric, &fidl_inspect::Metric { key: key.to_string(), value });
}
