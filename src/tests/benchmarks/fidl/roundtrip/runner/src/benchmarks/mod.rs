// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::BenchmarkFn;

mod bytes;
mod handles;
mod strings;

pub fn all() -> Vec<BenchmarkFn> {
    let mut benchmarks = vec![];
    benchmarks.append(&mut handles::all());
    benchmarks.append(&mut strings::all());
    benchmarks.append(&mut bytes::all());
    benchmarks
}
