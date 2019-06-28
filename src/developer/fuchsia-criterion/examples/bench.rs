// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use fuchsia_criterion::{criterion, FuchsiaCriterion};

fn fibonacci(n: u64) -> u64 {
    match n {
        0 => 1,
        1 => 1,
        n => fibonacci(n - 1) + fibonacci(n - 2),
    }
}

fn main() {
    let mut c = FuchsiaCriterion::new(
        criterion::Criterion::default()
            .warm_up_time(Duration::new(0, 0))
            .measurement_time(Duration::from_millis(1)),
    );

    c.bench_function("fib 10", |b| b.iter(|| fibonacci(criterion::black_box(10))));
}
