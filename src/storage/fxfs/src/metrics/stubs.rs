// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::metrics::traits::{Metric, NumericMetric},
    std::marker::PhantomData,
};

///
/// This file contains stubs for the various metric property types so that they can be used on
/// unsupported platforms.
///

/// Metric equivalent to [`fuchsia_inspect::StringProperty`].
pub type StringMetric<'a> = ScalarMetric<&'a str>;
/// Metric equivalent to [`fuchsia_inspect::IntProperty`].
pub type IntMetric = ScalarMetric<i64>;
/// Metric equivalent to [`fuchsia_inspect::UintProperty`].
pub type UintMetric = ScalarMetric<u64>;
/// Metric equivalent to [`fuchsia_inspect::DoubleProperty`].
pub type DoubleMetric = ScalarMetric<f64>;

/// Generic type to help implementing scalar metric types. Do not use directly - use named metric
/// types above instead (e.g. [`StringMetric`], [`IntMetric`]).
pub struct ScalarMetric<DataType> {
    phantom: PhantomData<DataType>,
}

impl<DataType> Metric<DataType> for ScalarMetric<DataType> {
    fn new(_name: &'static str, _value: DataType) -> Self {
        Self { phantom: PhantomData }
    }

    fn set(&self, _value: DataType) {}
}

impl<DataType> NumericMetric<DataType> for ScalarMetric<DataType> {
    fn add(&self, _value: DataType) {}

    fn subtract(&self, _value: DataType) {}

    fn set_if<BinaryPredicate>(&self, _value: DataType, _predicate: BinaryPredicate)
    where
        BinaryPredicate: FnOnce(DataType, DataType) -> bool,
    {
    }
}
