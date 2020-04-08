// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_criterion::FuchsiaCriterion;

mod benchmark_suite;

fn main() {
    let mut c = FuchsiaCriterion::default();
    benchmark_suite::benches(&mut c);
}
