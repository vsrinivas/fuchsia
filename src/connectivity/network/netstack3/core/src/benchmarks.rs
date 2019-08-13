// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! High-level benchmarks.
//!
//! This module contains end-to-end and other high-level benchmarks for the
//! netstack.

use packet::{Buf, BufferMut, InnerPacketBuilder, Serializer};
use rand_xorshift::XorShiftRng;
use std::time::{Duration, Instant};

use crate::device::ethernet::EtherType;
use crate::device::{receive_frame, DeviceId, DeviceLayerEventDispatcher};
use crate::ip::icmp::{IcmpConnId, IcmpEventDispatcher};
use crate::ip::IpProto;
use crate::testutil::benchmarks::{black_box, Bencher};
use crate::testutil::{DummyEventDispatcherBuilder, FakeCryptoRng, DUMMY_CONFIG_V4};
use crate::transport::udp::UdpEventDispatcher;
use crate::transport::TransportLayerEventDispatcher;
use crate::wire::ethernet::{
    EthernetFrameBuilder, ETHERNET_DST_MAC_BYTE_OFFSET, ETHERNET_HDR_LEN_NO_TAG,
    ETHERNET_MIN_BODY_LEN_NO_TAG, ETHERNET_SRC_MAC_BYTE_OFFSET,
};
use crate::wire::ipv4::{
    Ipv4PacketBuilder, IPV4_CHECKSUM_OFFSET, IPV4_MIN_HDR_LEN, IPV4_TTL_OFFSET,
};
use crate::{EventDispatcher, IpLayerEventDispatcher, StackStateBuilder, TimerId};

#[derive(Default)]
struct BenchmarkEventDispatcher;

impl UdpEventDispatcher for BenchmarkEventDispatcher {}

impl TransportLayerEventDispatcher for BenchmarkEventDispatcher {}

impl<B: BufferMut> DeviceLayerEventDispatcher<B> for BenchmarkEventDispatcher {
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        frame: S,
    ) -> Result<(), S> {
        black_box(frame.serialize_no_alloc_outer()).map_err(|(_, ser)| ser)?;
        Ok(())
    }
}

impl<B: BufferMut> IcmpEventDispatcher<B> for BenchmarkEventDispatcher {
    fn receive_icmp_echo_reply(&mut self, conn: IcmpConnId, seq_num: u16, data: B) {
        unimplemented!()
    }
}

impl<B: BufferMut> IpLayerEventDispatcher<B> for BenchmarkEventDispatcher {}

impl EventDispatcher for BenchmarkEventDispatcher {
    type Instant = std::time::Instant;

    fn now(&self) -> Self::Instant {
        unimplemented!()
    }

    fn schedule_timeout(&mut self, duration: Duration, id: TimerId) -> Option<Self::Instant> {
        unimplemented!()
    }

    fn schedule_timeout_instant(&mut self, time: Instant, id: TimerId) -> Option<Self::Instant> {
        unimplemented!()
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<Self::Instant> {
        None
    }

    fn cancel_timeouts_with<F: FnMut(&TimerId) -> bool>(&mut self, f: F) {
        unimplemented!()
    }

    type Rng = FakeCryptoRng<XorShiftRng>;

    fn rng(&mut self) -> &mut FakeCryptoRng<XorShiftRng> {
        unimplemented!()
    }
}

// Benchmark the minimum possible time to forward by stripping out all
// interesting computation. We have the simplest possible setup - a forwarding
// table with a single entry, and a single device - and we receive an IP packet
// frame which we expect will be parsed and forwarded without requiring any new
// buffers to be allocated.
//
// As of Change-Id Iaa22ea23620405dadd0b4f58d112781b29890a46, on a 2018 MacBook
// Pro, this benchmark takes in the neighborhood of 96ns - 100ns for all frame
// sizes.
fn bench_forward_minimum<B: Bencher>(b: &mut B, frame_size: usize) {
    let mut state_builder = StackStateBuilder::default();
    state_builder.ip_builder().forward(true);

    // Most tests do not need NDP's DAD or router solicitation so disable it here.
    let mut ndp_configs = crate::device::ndp::NdpConfigurations::default();
    ndp_configs.set_dup_addr_detect_transmits(None);
    ndp_configs.set_max_router_solicitations(None);
    state_builder.device_builder().set_default_ndp_configs(ndp_configs);

    let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V4)
        .build_with::<BenchmarkEventDispatcher>(state_builder, BenchmarkEventDispatcher);

    assert!(
        frame_size
            >= ETHERNET_HDR_LEN_NO_TAG
                + std::cmp::max(ETHERNET_MIN_BODY_LEN_NO_TAG, IPV4_MIN_HDR_LEN)
    );
    let mut body = vec![0; frame_size - (ETHERNET_HDR_LEN_NO_TAG + IPV4_MIN_HDR_LEN)];
    let mut buf = body
        .into_serializer()
        .encapsulate(Ipv4PacketBuilder::new(
            // Use the remote IP as the destination so that we decide to
            // forward.
            DUMMY_CONFIG_V4.remote_ip,
            DUMMY_CONFIG_V4.remote_ip,
            64,
            IpProto::Udp,
        ))
        .encapsulate(EthernetFrameBuilder::new(
            DUMMY_CONFIG_V4.remote_mac,
            DUMMY_CONFIG_V4.local_mac,
            EtherType::Ipv4,
        ))
        .serialize_vec_outer()
        .unwrap();

    let device = DeviceId::new_ethernet(0);
    let mut buf = buf.as_mut();
    let range = 0..buf.len();
    b.iter(|| {
        black_box(receive_frame(
            black_box(&mut ctx),
            black_box(device),
            black_box(Buf::new(&mut buf[..], range.clone())),
        ));
        // Since we modified the buffer in-place, it now has the wrong source
        // and destination MAC addresses and IP TTL. We reset them to their
        // original values as efficiently as we can to avoid affecting the
        // results of the benchmark.
        (&mut buf[ETHERNET_SRC_MAC_BYTE_OFFSET..ETHERNET_SRC_MAC_BYTE_OFFSET + 6])
            .copy_from_slice(&DUMMY_CONFIG_V4.remote_mac.bytes()[..]);
        (&mut buf[ETHERNET_DST_MAC_BYTE_OFFSET..ETHERNET_DST_MAC_BYTE_OFFSET + 6])
            .copy_from_slice(&DUMMY_CONFIG_V4.local_mac.bytes()[..]);
        buf[ETHERNET_HDR_LEN_NO_TAG + IPV4_TTL_OFFSET] = 64;
        buf[IPV4_CHECKSUM_OFFSET] = 249;
        buf[IPV4_CHECKSUM_OFFSET + 1] = 102;
    });
}

bench!(bench_forward_minimum_64, |b| bench_forward_minimum(b, 64));
bench!(bench_forward_minimum_128, |b| bench_forward_minimum(b, 128));
bench!(bench_forward_minimum_256, |b| bench_forward_minimum(b, 256));
bench!(bench_forward_minimum_512, |b| bench_forward_minimum(b, 512));
bench!(bench_forward_minimum_1024, |b| bench_forward_minimum(b, 1024));
