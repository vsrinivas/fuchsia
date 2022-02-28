// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///
/// The [`metrics`] module implements a non-intrusive way of recording filesystem-specific metrics.
/// Example usage:
///
///     use crate::metrics::IntMetric;
///     let my_metric = IntMetric::new("metric_name", /*initial_value*/ 0);
///     my_metric.set(5);
///
/// Any metrics created will be available under the "fxfs/fs.detail" inspect node, which can be
/// queried by running `ffx inspect show bootstrap/fshost:root/fxfs`.
///
/// The metric will remain available in the inspect tree until the metric object is dropped.
///
/// Similar names to the property types from [`fuchsia_inspect`] are provided, but with `Property`
/// replaced with `Metric` to prevent any confusion. For example, [`metrics::IntMetric`] acts as a
/// drop-in replacement for [`fuchsia_inspect::IntProperty`]. The types provided in this module also
/// extend the functionality provided by [`fuchsia_inspect`].
///
/// Metric collection on host builds is not supported, but a functionally equivalent stub
/// implementation is provided to allow the library to work cross-platform.
///
pub mod traits;

//----------------------------------------------------------------------
// Platform-Specific Modules/Exports
//----------------------------------------------------------------------

// Export the stub implementations for non-Fuchsia builds.
#[cfg(not(target_os = "fuchsia"))]
mod stubs;
#[cfg(not(target_os = "fuchsia"))]
pub use stubs::*;

// Export the `fuchsia_inspect`-backed implementation on Fuchsia.
#[cfg(target_os = "fuchsia")]
mod fuchsia;
#[cfg(target_os = "fuchsia")]
pub use self::fuchsia::*;
