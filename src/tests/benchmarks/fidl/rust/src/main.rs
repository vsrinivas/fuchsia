// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    benchmark_suite,
    criterion::Criterion,
    fuchsia_criterion::{criterion::Benchmark, FuchsiaCriterion},
    std::{mem, time::Duration},
};
fn main() {
    // TODO(fxbug.dev/52171): Do not use Criterion.
    let all = &benchmark_suite::ALL_BENCHMARKS;
    let mut benchmark_defs = all.iter().copied().flatten();
    let (first_label, first_function) = benchmark_defs.next().unwrap();
    let mut benchmark = Benchmark::new(wall_time_label(first_label), first_function);
    for (label, function) in benchmark_defs {
        benchmark = benchmark.with_function(wall_time_label(label), function);
    }
    // FuchsiaCriterion is a wrapper around Criterion. To configure the inner
    // Criterion we have to use a strange, indirect approach. This is because
    // FuchsiaCriterion only provides access to it via DerefMut, and Criterion
    // only provides a builder API (i.e. consuming self) for configuration.
    let mut fc = FuchsiaCriterion::default();
    let c: &mut Criterion = &mut fc;
    *c = mem::take(c)
        .warm_up_time(Duration::from_millis(50))
        .measurement_time(Duration::from_millis(1000))
        // We must reduce the sample size from the default of 100, otherwise
        // Criterion will sometimes override the 50ms + 1000ms suggested times
        // and run for much longer.
        .sample_size(10);
    fc.bench("fuchsia.fidl_microbenchmarks", benchmark);
}
fn wall_time_label(base: &str) -> String {
    format!("Rust/{}/WallTime", base)
}
