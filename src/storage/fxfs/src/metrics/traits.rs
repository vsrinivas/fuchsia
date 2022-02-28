// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///
/// This module contains traits that have signatures similar to the corresponding property traits
/// from [`fuchsia_inspect`], but with "Property" replaced with "Metric".
///

/// Wrapper for the [`fuchsia_inspect::Property`] trait. Used for all scalar metric types (e.g.
/// [`crate::metrics::StringMetric`], [`crate::metrics::IntMetric`]).
pub trait Metric<DataType> {
    /// Create a new metric with the given `name` and `value`.
    /// `name` should be a unique string identifier.
    fn new(name: &'static str, value: DataType) -> Self;

    /// Set the current value of this metric to `value`.
    fn set(&self, value: DataType);
}

/// Wrapper for [`fuchsia_inspect::NumericProperty`] traits. Used for [`crate::metrics::IntMetric`],
/// [`crate::metrics::UintMetric`], and [`crate::metrics::DoubleMetric`] types.
pub trait NumericMetric<DataType>: Metric<DataType> {
    /// Add `value` to the current value of this metric.
    /// For integral types, saturating addition is used.
    fn add(&self, value: DataType);

    /// Subtract `value` from the current value of this metric.
    /// For integral types, saturating subtraction is used.
    fn subtract(&self, value: DataType);

    /// Set the value if a binary predicate is satisfied.
    ///
    /// # Arguments
    ///
    /// * `value` - Value to set this metric to if `predicate` is satisfied.
    /// * `predicate` - Binary operation that compares the current metric value and `value`. If the
    ///     predicate returns true, the metric will be updated to `value`.
    ///
    /// # Examples
    /// ```
    /// let int_metric = metrics::IntMetric("int_metric", 0);
    /// int_metric.set_if(-2, |curr, new| new > curr);
    /// assert_eq!(int_metric.get(), 0);
    /// int_metric.set_if(4, |curr, new| new > curr);
    /// assert_eq!(int_metric.get(), 4);
    /// ```
    fn set_if<BinaryPredicate>(&self, value: DataType, predicate: BinaryPredicate)
    where
        BinaryPredicate: FnOnce(DataType, DataType) -> bool;
}
