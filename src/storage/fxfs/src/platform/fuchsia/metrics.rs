// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//!
//! This module contains implementations for metric property types backed by [`fuchsia_inspect`].
//! Any metrics created will be attached to the fs.detail node in the fxfs inspect tree and remain
//! available until the metric object itself is dropped.
//!

use {
    crate::metrics::traits::{Metric, NumericMetric},
    fuchsia_inspect::{NumericProperty, Property},
    once_cell::sync::Lazy,
    std::string::String,
    std::sync::Mutex,
};

/// Metric equivalent to [`fuchsia_inspect::StringProperty`].
pub type StringMetric = ScalarMetric<fuchsia_inspect::StringProperty>;
/// Metric equivalent to [`fuchsia_inspect::IntProperty`].
pub type IntMetric = ScalarMetric<fuchsia_inspect::IntProperty>;
/// Metric equivalent to [`fuchsia_inspect::UintProperty`].
pub type UintMetric = ScalarMetric<fuchsia_inspect::UintProperty>;
/// Metric equivalent to [`fuchsia_inspect::DoubleProperty`].
pub type DoubleMetric = ScalarMetric<fuchsia_inspect::DoubleProperty>;

/// Root "fxfs" node to which all filesystem metrics will be attached.
///
/// We cannot attach properties directly to the root node as all Inspect trees are forwarded by
/// fshost, and thus we need a uniquely named root node in order for Inspect queries to
/// differentiate different filesystems.
pub static FXFS_ROOT_NODE: Lazy<Mutex<fuchsia_inspect::Node>> =
    Lazy::new(|| Mutex::new(fuchsia_inspect::component::inspector().root().create_child("fxfs")));

/// "fs.detail" node on which all constructed metrics will be attached.
pub static DETAIL_NODE: Lazy<Mutex<fuchsia_inspect::Node>> =
    Lazy::new(|| Mutex::new(FXFS_ROOT_NODE.lock().unwrap().create_child("fs.detail")));

/// Node which contains an entry for each object store.
pub static OBJECT_STORES_NODE: Lazy<Mutex<fuchsia_inspect::Node>> =
    Lazy::new(|| Mutex::new(DETAIL_NODE.lock().unwrap().create_child("stores")));

/// Generic type to help implementing scalar metrics. Use named type definitions instead (e.g.
/// [`StringMetric`], [`UintMetric`]).
pub struct ScalarMetric<InspectType> {
    inner: Mutex<InspectType>,
}

impl Metric<String> for StringMetric {
    fn new(name: impl AsRef<str>, value: String) -> Self {
        Self { inner: Mutex::new(DETAIL_NODE.lock().unwrap().create_string(name.as_ref(), value)) }
    }

    fn set(&self, value: String) {
        self.inner.lock().unwrap().set(value.as_str())
    }
}

impl Metric<i64> for IntMetric {
    fn new(name: impl AsRef<str>, value: i64) -> Self {
        Self { inner: Mutex::new(DETAIL_NODE.lock().unwrap().create_int(name.as_ref(), value)) }
    }

    fn set(&self, value: i64) {
        self.inner.lock().unwrap().set(value)
    }
}

impl Metric<u64> for UintMetric {
    fn new(name: impl AsRef<str>, value: u64) -> Self {
        Self { inner: Mutex::new(DETAIL_NODE.lock().unwrap().create_uint(name.as_ref(), value)) }
    }

    fn set(&self, value: u64) {
        self.inner.lock().unwrap().set(value)
    }
}

impl Metric<f64> for DoubleMetric {
    fn new(name: impl AsRef<str>, value: f64) -> Self {
        Self { inner: Mutex::new(DETAIL_NODE.lock().unwrap().create_double(name.as_ref(), value)) }
    }

    fn set(&self, value: f64) {
        self.inner.lock().unwrap().set(value)
    }
}

impl<'a, DataType, InspectType> NumericMetric<DataType> for ScalarMetric<InspectType>
where
    Self: Metric<DataType>,
    DataType: Copy,
    InspectType: NumericProperty<'a, Type = DataType>,
{
    fn add(&self, value: DataType) {
        self.inner.lock().unwrap().add(value)
    }

    fn subtract(&self, value: DataType) {
        self.inner.lock().unwrap().subtract(value)
    }

    fn set_if<BinaryPredicate>(&self, value: DataType, predicate: BinaryPredicate)
    where
        BinaryPredicate: FnOnce(DataType, DataType) -> bool,
    {
        let inner_locked = self.inner.lock().unwrap();
        let current = inner_locked.get().unwrap();
        if predicate(current, value) {
            inner_locked.set(value)
        }
    }
}
