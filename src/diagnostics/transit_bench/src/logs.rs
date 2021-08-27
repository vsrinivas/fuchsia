// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_criterion::criterion::{Bencher, Criterion, ParameterizedBenchmark, Throughput},
    log::info,
    rand::{
        distributions::{Alphanumeric, Distribution},
        thread_rng,
    },
};

const PREFIX: &str = "bogus";

pub fn benches(c: &mut Criterion) {
    let simple_log_bench =
        ParameterizedBenchmark::new("simple_log", simple_log, vec![1, 5, 13, 16])
            .throughput(|&len| Throughput::Bytes((2usize.pow(len) + PREFIX.len()) as u32));
    c.bench("logging", simple_log_bench);
}

fn simple_log(b: &mut Bencher, &msg_len: &u32) {
    let mut rng = thread_rng();
    let s: String = Alphanumeric.sample_iter(&mut rng).take(2usize.pow(msg_len)).collect();
    b.iter(|| {
        info!("{}{}", PREFIX, s);
    });
}
