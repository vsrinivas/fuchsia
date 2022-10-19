// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//!
//! The [`metrics`] module implements a non-intrusive way of recording filesystem-specific metrics.
//! Example usage:
//!
//!     use crate::metrics::IntMetric;
//!     let my_metric = IntMetric::new("metric_name", /*initial_value*/ 0);
//!     my_metric.set(5);
//!
//! Any metrics created will be available under the "fxfs/fs.detail" inspect node, which can be
//! queried by running `ffx inspect show bootstrap/fshost:root/fxfs`.
//!
//! The metric will remain available in the inspect tree until the metric object is dropped.
//!
//! Similar names to the property types from [`fuchsia_inspect`] are provided, but with `Property`
//! replaced with `Metric` to prevent any confusion. For example, [`metrics::IntMetric`] acts as a
//! drop-in replacement for [`fuchsia_inspect::IntProperty`]. The types provided in this module also
//! extend the functionality provided by [`fuchsia_inspect`].
//!

pub mod traits;

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
#[cfg(target_os = "fuchsia")]
pub static FXFS_ROOT_NODE: Lazy<Mutex<fuchsia_inspect::Node>> =
    Lazy::new(|| Mutex::new(fuchsia_inspect::component::inspector().root().create_child("fxfs")));
#[cfg(not(target_os = "fuchsia"))]
pub static FXFS_ROOT_NODE: Lazy<Mutex<fuchsia_inspect::Node>> =
    Lazy::new(|| Mutex::new(fuchsia_inspect::Node::default()));

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
        if let Ok(current) = inner_locked.get() {
            if predicate(current, value) {
                inner_locked.set(value)
            }
        }
    }
}
