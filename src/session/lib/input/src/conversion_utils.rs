// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::Range;

/// Converts an input report axis to a [`std::ops::Range`].
///
/// # Parameters
/// - `axis`: The axis to extract the range from.
pub fn to_range(axis: fidl_fuchsia_input_report::Axis) -> Range<i64> {
    std::ops::Range { start: axis.range.min, end: axis.range.max + 1 }
}
