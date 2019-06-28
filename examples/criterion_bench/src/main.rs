// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_criterion::{criterion, FuchsiaCriterion};

fn fib(n: u64) -> u64 {
    match n {
        0 => 1,
        1 => 1,
        n => fib(n - 1) + fib(n - 2),
    }
}

fn main() {
    let mut c = FuchsiaCriterion::default();
    c.bench_function("fib(20)", |b| b.iter(|| fib(criterion::black_box(20))));
}
