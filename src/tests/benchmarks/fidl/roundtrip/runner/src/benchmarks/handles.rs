// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::BenchmarkFn;

use fuchsia_zircon as zx;

fn make_channels(count: usize) -> Vec<zx::Handle> {
    (0..(count + 1) / 2)
        .map(|_| zx::Channel::create().unwrap())
        .map(|(a, b)| vec![a.into(), b.into()])
        .flatten()
        .take(count)
        .collect()
}

pub fn all() -> Vec<BenchmarkFn> {
    vec![BenchmarkFn::new(
        "Channels",
        |num_channels| format!("{}", num_channels),
        vec![1, 8, 64],
        move |count| make_channels(count),
        |handles, proxy| async move {
            let call_fut = proxy.echo_handles(&mut handles.into_iter());
            call_fut.await.unwrap()
        },
    )]
}
