// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A benchmark runner for Netstack3, based on Criterion.
use fuchsia_criterion::{criterion::Criterion, FuchsiaCriterion};

pub(crate) fn main() {
    let core_benches = netstack3_core_benchmarks::benchmarks::get_benchmark();

    let mut c = FuchsiaCriterion::default();
    let _: &mut Criterion = c.bench("fuchsia.netstack3.core", core_benches);
}
