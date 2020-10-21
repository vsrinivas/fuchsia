// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_criterion::criterion::{black_box, Bencher, Criterion},
    fuchsia_zircon as zx,
};

pub fn benches(c: &mut Criterion) {
    c.bench_function("ticks_get", ticks_get);
    c.bench_function("monotonic_time", monotonic_time);
}

fn ticks_get(b: &mut Bencher) {
    b.iter(|| black_box(zx::ticks_get()));
}

fn monotonic_time(b: &mut Bencher) {
    b.iter(|| black_box(zx::Time::get_monotonic()));
}
