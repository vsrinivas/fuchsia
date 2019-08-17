// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_criterion::criterion::{Bencher, Criterion},
    fuchsia_zircon as zx,
};

pub fn benches(c: &mut Criterion) {
    c.bench_function_over_inputs(
        "channel_write_read_zeroes",
        channel_write_read_zeroes,
        [64, 1024, 32 * 1024, 64 * 1024].into_iter(),
    );
}

fn channel_write_read_zeroes(b: &mut Bencher, message_size: &&usize) {
    let message = vec![0; **message_size];
    let mut handles = vec![];
    let (one, two) = zx::Channel::create().unwrap();

    b.iter(|| {
        one.write(&message, &mut handles).unwrap();
        let mut output = zx::MessageBuf::new();
        two.read(&mut output).unwrap();
    });
}
