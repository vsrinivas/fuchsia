// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::{
    encoding::{Context, Decodable, Decoder, Encoder},
    fidl_struct,
    handle::Handle,
};
use fuchsia_criterion::criterion::{BatchSize, Bencher, Benchmark, Criterion};

const V1_CONTEXT: &Context = &Context {};

#[derive(Debug, PartialEq)]
pub struct Int64Struct {
    x: u64,
}
fidl_struct! {
    name: Int64Struct,
    members: [
        x {
            ty: u64,
            offset_v1: 0,
        },
    ],
    size_v1: 8,
    align_v1: 8,
}

pub fn benches(c: &mut Criterion) {
    // We need to manually create a Benchmarks and call Criterion::Bench in order to correctly get
    // the `test_suite` and `label` fields correct - using the suggested bench_function methods
    // leads to `test_suite` taking on the test name, and `label` being empty. Also, there's no way
    // to initialize an empty Benchmark, so the first routine must be added using Benchmark::new,
    // and subsequent ones using `with_function`. With an updated criterion crate, this should just
    // use the Criterion::benchmark_group instead.
    let bench = Benchmark::new("Rust/Encode/Int64Struct/WallTime", int64struct_encode)
        .with_function("Rust/Decode/Int64Struct/WallTime", int64struct_decode);
    c.bench("fuchsia.fidl_microbenchmarks", bench);
}

/// Benchmark encoding a FIDL struct containing a single uint64 field
fn int64struct_encode(b: &mut Bencher) {
    b.iter_batched(
        || {
            let buffer: Vec<u8> = Vec::with_capacity(1024);
            let handles: Vec<Handle> = Vec::new();
            let value = Int64Struct { x: 0x0102030405060708 };
            (buffer, handles, value)
        },
        |test_data| {
            let (mut buffer, mut handles, mut value) = test_data;
            Encoder::encode_with_context(V1_CONTEXT, &mut buffer, &mut handles, &mut value)
                .unwrap();
        },
        BatchSize::SmallInput,
    );
}

/// Benchmark decoding a FIDL struct containing a single uint64 field
fn int64struct_decode(b: &mut Bencher) {
    b.iter_batched(
        || {
            let bytes = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];
            let handles = [];
            let value = Int64Struct::new_empty();
            (bytes, handles, value)
        },
        |test_data| {
            let (mut bytes, mut handles, mut value) = test_data;
            Decoder::decode_with_context(V1_CONTEXT, &mut bytes, &mut handles, &mut value).unwrap();
        },
        BatchSize::SmallInput,
    );
}
