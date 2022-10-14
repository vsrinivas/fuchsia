// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! High-level benchmarks.
//!
//! This module contains microbenchmarks for the Netstack3 Core, built on top
//! of Criterion.

// Enable dead code warnings for benchmarks (disabled in `lib.rs`), unless
// fuzzing is enabled.
#![cfg_attr(not(fuzz), warn(dead_code, unused_imports, unused_macros))]

use alloc::vec;

use net_types::{ip::Ipv4, Witness as _};
use packet::{Buf, InnerPacketBuilder, Serializer};
use packet_formats::{
    ethernet::{
        testutil::{
            ETHERNET_DST_MAC_BYTE_OFFSET, ETHERNET_HDR_LEN_NO_TAG, ETHERNET_MIN_BODY_LEN_NO_TAG,
            ETHERNET_SRC_MAC_BYTE_OFFSET,
        },
        EtherType, EthernetFrameBuilder,
    },
    ip::IpProto,
    ipv4::{
        testutil::{IPV4_CHECKSUM_OFFSET, IPV4_MIN_HDR_LEN, IPV4_TTL_OFFSET},
        Ipv4PacketBuilder,
    },
};

use crate::{
    device::receive_frame,
    testutil::{
        benchmarks::{black_box, Bencher},
        FakeEventDispatcherBuilder, FAKE_CONFIG_V4,
    },
    Ctx, StackStateBuilder,
};

// NOTE: Extra tests that are too expensive to run during benchmarks can be
// added by gating them on the `debug_assertions` configuration option. This
// option is disabled when running `cargo check`, but enabled when running
// `cargo test`.

// Benchmark the minimum possible time to forward an IPv4 packet by stripping
// out all interesting computation. We have the simplest possible setup - a
// forwarding table with a single entry, and a single device - and we receive an
// IPv4 packet frame which we expect will be parsed and forwarded without
// requiring any new buffers to be allocated.
fn bench_forward_minimum<B: Bencher>(b: &mut B, frame_size: usize) {
    let (Ctx { sync_ctx, mut non_sync_ctx }, idx_to_device_id) =
        FakeEventDispatcherBuilder::from_config(FAKE_CONFIG_V4)
            .build_with(StackStateBuilder::default());
    let mut sync_ctx = &sync_ctx;
    let device = &idx_to_device_id[0];
    crate::ip::device::set_routing_enabled::<_, _, Ipv4>(
        &mut sync_ctx,
        &mut non_sync_ctx,
        device,
        true,
    )
    .expect("error setting routing enabled");

    assert!(
        frame_size
            >= ETHERNET_HDR_LEN_NO_TAG
                + core::cmp::max(ETHERNET_MIN_BODY_LEN_NO_TAG, IPV4_MIN_HDR_LEN)
    );
    let body = vec![0; frame_size - (ETHERNET_HDR_LEN_NO_TAG + IPV4_MIN_HDR_LEN)];
    const TTL: u8 = 64;
    let mut buf = body
        .into_serializer()
        .encapsulate(Ipv4PacketBuilder::new(
            // Use the remote IP as the destination so that we decide to
            // forward.
            FAKE_CONFIG_V4.remote_ip,
            FAKE_CONFIG_V4.remote_ip,
            TTL,
            IpProto::Udp.into(),
        ))
        .encapsulate(EthernetFrameBuilder::new(
            FAKE_CONFIG_V4.remote_mac.get(),
            FAKE_CONFIG_V4.local_mac.get(),
            EtherType::Ipv4,
        ))
        .serialize_vec_outer()
        .unwrap();

    let buf = buf.as_mut();
    let range = 0..buf.len();

    // Store a copy of the checksum to re-write it later.
    let ipv4_checksum = [
        buf[ETHERNET_HDR_LEN_NO_TAG + IPV4_CHECKSUM_OFFSET],
        buf[ETHERNET_HDR_LEN_NO_TAG + IPV4_CHECKSUM_OFFSET + 1],
    ];

    #[cfg(debug_assertions)]
    let mut iters = 0;
    b.iter(|| {
        #[cfg(debug_assertions)]
        {
            iters += 1;
        }
        black_box(
            receive_frame(
                black_box(&mut sync_ctx),
                black_box(&mut non_sync_ctx),
                black_box(device),
                black_box(Buf::new(&mut buf[..], range.clone())),
            )
            .expect("error receiving frame"),
        );

        #[cfg(debug_assertions)]
        {
            assert_eq!(non_sync_ctx.frames_sent().len(), iters);
        }

        // Since we modified the buffer in-place, it now has the wrong source
        // and destination MAC addresses and IP TTL/CHECKSUM. We reset them to
        // their original values as efficiently as we can to avoid affecting the
        // results of the benchmark.
        (&mut buf[ETHERNET_SRC_MAC_BYTE_OFFSET..ETHERNET_SRC_MAC_BYTE_OFFSET + 6])
            .copy_from_slice(&FAKE_CONFIG_V4.remote_mac.bytes()[..]);
        (&mut buf[ETHERNET_DST_MAC_BYTE_OFFSET..ETHERNET_DST_MAC_BYTE_OFFSET + 6])
            .copy_from_slice(&FAKE_CONFIG_V4.local_mac.bytes()[..]);
        let ipv4_buf = &mut buf[ETHERNET_HDR_LEN_NO_TAG..];
        ipv4_buf[IPV4_TTL_OFFSET] = TTL;
        ipv4_buf[IPV4_CHECKSUM_OFFSET..IPV4_CHECKSUM_OFFSET + 2]
            .copy_from_slice(&ipv4_checksum[..]);
    });
}

bench!(bench_forward_minimum_64, |b| bench_forward_minimum(b, 64));
bench!(bench_forward_minimum_128, |b| bench_forward_minimum(b, 128));
bench!(bench_forward_minimum_256, |b| bench_forward_minimum(b, 256));
bench!(bench_forward_minimum_512, |b| bench_forward_minimum(b, 512));
bench!(bench_forward_minimum_1024, |b| bench_forward_minimum(b, 1024));

#[cfg(benchmark)]
/// Returns a benchmark group for all Netstack3 Core microbenchmarks.
pub fn get_benchmark() -> criterion::Benchmark {
    // TODO(http://fxbug.dev/100863) Find an automatic way to add benchmark
    // functions to the `Criterion::Benchmark`, ideally as part of `bench!`.
    let mut b = criterion::Benchmark::new("ForwardIpv4/64", bench_forward_minimum_64);
    b = b.with_function("ForwardIpv4/128", bench_forward_minimum_128);
    b = b.with_function("ForwardIpv4/256", bench_forward_minimum_256);
    b = b.with_function("ForwardIpv4/512", bench_forward_minimum_512);
    b = b.with_function("ForwardIpv4/1024", bench_forward_minimum_1024);

    // Add additional microbenchmarks defined elsewhere.
    crate::data_structures::token_bucket::tests::add_benches(b)
}
