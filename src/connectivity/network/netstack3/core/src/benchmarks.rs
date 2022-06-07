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
use core::time::Duration;

use net_types::{ip::Ipv4, Witness as _};
use packet::{Buf, BufferMut, InnerPacketBuilder, Serializer};
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
use rand_xorshift::XorShiftRng;

use crate::{
    context::{testutil::DummyInstant, EventContext, InstantContext, RngContext, TimerContext},
    device::{receive_frame, DeviceId, DeviceLayerEventDispatcher},
    ip::icmp::{BufferIcmpContext, IcmpConnId, IcmpContext, IcmpIpExt},
    testutil::{
        benchmarks::{black_box, Bencher},
        DummyEventDispatcherBuilder, FakeCryptoRng, DUMMY_CONFIG_V4,
    },
    transport::udp::{BufferUdpContext, UdpContext},
    {StackStateBuilder, TimerId},
};

// NOTE: Extra tests that are too expensive to run during benchmarks can be
// added by gating them on the `debug_assertions` configuration option. This
// option is disabled when running `cargo check`, but enabled when running
// `cargo test`.

#[derive(Default)]
struct BenchmarkEventDispatcher {
    #[cfg(debug_assertions)]
    frames_sent: usize,
}

#[derive(Default)]
struct BenchmarkCoreContext {
    rng: FakeCryptoRng<XorShiftRng>,
}

impl<I: IcmpIpExt> UdpContext<I> for BenchmarkEventDispatcher {}

impl<I: crate::ip::IpExt, B: BufferMut> BufferUdpContext<I, B> for BenchmarkEventDispatcher {}

impl<B: BufferMut> DeviceLayerEventDispatcher<B> for BenchmarkEventDispatcher {
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        _device: DeviceId,
        frame: S,
    ) -> Result<(), S> {
        let _: B = black_box(frame.serialize_no_alloc_outer()).map_err(|(_, ser)| ser)?;
        #[cfg(debug_assertions)]
        {
            self.frames_sent += 1;
        }
        Ok(())
    }
}

impl<I: IcmpIpExt> IcmpContext<I> for BenchmarkEventDispatcher {
    fn receive_icmp_error(&mut self, _conn: IcmpConnId<I>, _seq_num: u16, _err: I::ErrorCode) {
        unimplemented!()
    }
}

impl<I: IcmpIpExt, B: BufferMut> BufferIcmpContext<I, B> for BenchmarkEventDispatcher {
    fn receive_icmp_echo_reply(
        &mut self,
        _conn: IcmpConnId<I>,
        _src_ip: I::Addr,
        _dst_ip: I::Addr,
        _id: u16,
        _seq_num: u16,
        _data: B,
    ) {
        unimplemented!()
    }
}

impl<T> EventContext<T> for BenchmarkEventDispatcher {
    fn on_event(&mut self, _event: T) {}
}

impl InstantContext for BenchmarkCoreContext {
    type Instant = DummyInstant;

    fn now(&self) -> DummyInstant {
        DummyInstant::default()
    }
}

impl RngContext for BenchmarkCoreContext {
    type Rng = FakeCryptoRng<XorShiftRng>;

    fn rng(&self) -> &FakeCryptoRng<XorShiftRng> {
        &self.rng
    }

    fn rng_mut(&mut self) -> &mut FakeCryptoRng<XorShiftRng> {
        &mut self.rng
    }
}

impl TimerContext<TimerId> for BenchmarkCoreContext {
    fn schedule_timer(&mut self, _duration: Duration, _id: TimerId) -> Option<DummyInstant> {
        unimplemented!()
    }

    fn schedule_timer_instant(
        &mut self,
        _time: DummyInstant,
        _id: TimerId,
    ) -> Option<DummyInstant> {
        unimplemented!()
    }

    fn cancel_timer(&mut self, _id: TimerId) -> Option<DummyInstant> {
        None
    }

    fn cancel_timers_with<F: FnMut(&TimerId) -> bool>(&mut self, _f: F) {
        unimplemented!()
    }

    fn scheduled_instant(&self, _id: TimerId) -> Option<DummyInstant> {
        unimplemented!()
    }
}

// Benchmark the minimum possible time to forward an IPv4 packet by stripping
// out all interesting computation. We have the simplest possible setup - a
// forwarding table with a single entry, and a single device - and we receive an
// IPv4 packet frame which we expect will be parsed and forwarded without
// requiring any new buffers to be allocated.
fn bench_forward_minimum<B: Bencher>(b: &mut B, frame_size: usize) {
    let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V4).build_with(
        StackStateBuilder::default(),
        BenchmarkEventDispatcher::default(),
        BenchmarkCoreContext::default(),
    );
    crate::ip::device::set_routing_enabled::<_, _, Ipv4>(
        &mut ctx,
        &mut (),
        DeviceId::new_ethernet(0),
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
            DUMMY_CONFIG_V4.remote_ip,
            DUMMY_CONFIG_V4.remote_ip,
            TTL,
            IpProto::Udp.into(),
        ))
        .encapsulate(EthernetFrameBuilder::new(
            DUMMY_CONFIG_V4.remote_mac.get(),
            DUMMY_CONFIG_V4.local_mac.get(),
            EtherType::Ipv4,
        ))
        .serialize_vec_outer()
        .unwrap();

    let device = DeviceId::new_ethernet(0);
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
                black_box(&mut ctx),
                black_box(device),
                black_box(Buf::new(&mut buf[..], range.clone())),
            )
            .expect("error receiving frame"),
        );

        #[cfg(debug_assertions)]
        {
            assert_eq!(ctx.sync_ctx.dispatcher.frames_sent, iters);
        }

        // Since we modified the buffer in-place, it now has the wrong source
        // and destination MAC addresses and IP TTL/CHECKSUM. We reset them to
        // their original values as efficiently as we can to avoid affecting the
        // results of the benchmark.
        (&mut buf[ETHERNET_SRC_MAC_BYTE_OFFSET..ETHERNET_SRC_MAC_BYTE_OFFSET + 6])
            .copy_from_slice(&DUMMY_CONFIG_V4.remote_mac.bytes()[..]);
        (&mut buf[ETHERNET_DST_MAC_BYTE_OFFSET..ETHERNET_DST_MAC_BYTE_OFFSET + 6])
            .copy_from_slice(&DUMMY_CONFIG_V4.local_mac.bytes()[..]);
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
