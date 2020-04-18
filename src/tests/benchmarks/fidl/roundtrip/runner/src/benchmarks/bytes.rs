// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::BenchmarkFn;

pub fn all() -> Vec<BenchmarkFn> {
    vec![BenchmarkFn::new(
        "Bytes",
        |length| format!("{}Bytes", length),
        vec![16, 256, 4096],
        |length| vec![42_u8; length],
        |v, proxy| async move {
            let call_fut = proxy.echo_bytes(&v);
            call_fut.await.unwrap()
        },
    )]
}
