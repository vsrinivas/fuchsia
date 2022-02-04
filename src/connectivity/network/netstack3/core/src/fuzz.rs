// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(dead_code, unused_imports, unused_macros)]

use core::time::Duration;
use std::convert::TryInto as _;

use crate::{testutil::DummyEventDispatcher, Ctx, DeviceId, TimerId};
use arbitrary::{Arbitrary, Unstructured};
use fuzz_util::{zerocopy::ArbitraryFromBytes, Fuzzed};
use net_declare::net_mac;
use net_types::{
    ip::{IpAddress, Ipv4Addr},
    UnicastAddr,
};
use packet::{
    serialize::{Buf, SerializeError},
    Nested, NestedPacketBuilder, Serializer,
};
use packet_formats::{
    ethernet::EthernetFrameBuilder, ipv4::Ipv4PacketBuilder, udp::UdpPacketBuilder,
};

fn initialize_logging() {
    #[cfg(fuzz_logging)]
    {
        static LOGGER_ONCE: core::sync::atomic::AtomicBool =
            core::sync::atomic::AtomicBool::new(true);

        // This function gets called on every fuzz iteration, but we only need to set up logging the
        // first time.
        if LOGGER_ONCE.swap(false, core::sync::atomic::Ordering::AcqRel) {
            fuchsia_syslog::init().expect("couldn't initialize logging");
            log::info!("trace logs enabled");
            fuchsia_syslog::set_severity(fuchsia_syslog::levels::TRACE);
        };
    }
}

/// Wrapper around Duration that limits the range of possible values. This keeps the fuzzer
/// from generating Duration values that, when added up, would cause overflow.
struct SmallDuration(Duration);

impl<'a> Arbitrary<'a> for SmallDuration {
    fn arbitrary(u: &mut Unstructured<'a>) -> arbitrary::Result<Self> {
        // The maximum time increment to advance by in a single step.
        const MAX_DURATION: Duration = Duration::from_secs(60 * 60);

        let max = MAX_DURATION.as_nanos().try_into().unwrap();
        let nanos = u.int_in_range::<u64>(0..=max)?;
        Ok(Self(Duration::from_nanos(nanos)))
    }
}

#[derive(Copy, Clone, Debug, Arbitrary)]
enum FrameType {
    Raw,
    EthernetWith(EthernetFrameType),
}

#[derive(Copy, Clone, Debug, Arbitrary)]
enum EthernetFrameType {
    Raw,
    Ipv4(IpFrameType),
}

#[derive(Copy, Clone, Debug, Arbitrary)]
enum IpFrameType {
    Raw,
    Udp,
}

impl FrameType {
    fn arbitrary_buf(&self, u: &mut Unstructured<'_>) -> arbitrary::Result<Buf<Vec<u8>>> {
        match self {
            FrameType::Raw => Ok(Buf::new(u.arbitrary()?, ..)),
            FrameType::EthernetWith(ether_type) => {
                let builder = Fuzzed::<EthernetFrameBuilder>::arbitrary(u)?.into();
                ether_type.arbitrary_buf(builder, u)
            }
        }
    }
}

impl EthernetFrameType {
    fn arbitrary_buf<O: NestedPacketBuilder + std::fmt::Debug>(
        &self,
        outer: O,
        u: &mut Unstructured<'_>,
    ) -> arbitrary::Result<Buf<Vec<u8>>> {
        match self {
            EthernetFrameType::Raw => arbitrary_packet(outer, u),
            EthernetFrameType::Ipv4(ip_type) => {
                let ip_builder = Fuzzed::<Ipv4PacketBuilder>::arbitrary(u)?.into();
                ip_type.arbitrary_buf::<Ipv4Addr, _>(ip_builder.encapsulate(outer), u)
            }
        }
    }
}

impl IpFrameType {
    fn arbitrary_buf<
        'a,
        A: IpAddress + ArbitraryFromBytes<'a>,
        O: NestedPacketBuilder + std::fmt::Debug,
    >(
        &self,
        outer: O,
        u: &mut Unstructured<'a>,
    ) -> arbitrary::Result<Buf<Vec<u8>>> {
        match self {
            IpFrameType::Raw => arbitrary_packet(outer, u),
            IpFrameType::Udp => {
                let udp_builder = Fuzzed::<UdpPacketBuilder<A>>::arbitrary(u)?.into();
                arbitrary_packet(udp_builder.encapsulate(outer), u)
            }
        }
    }
}

struct ArbitraryFrame(FrameType, Buf<Vec<u8>>);

impl<'a> Arbitrary<'a> for ArbitraryFrame {
    fn arbitrary(u: &mut Unstructured<'a>) -> arbitrary::Result<Self> {
        let frame_type = u.arbitrary::<FrameType>()?;
        Ok(Self(frame_type, frame_type.arbitrary_buf(u)?))
    }
}

#[derive(Arbitrary)]
enum FuzzAction {
    ReceiveFrame(ArbitraryFrame),
    AdvanceTime(SmallDuration),
}

#[derive(Arbitrary)]
pub(crate) struct FuzzInput {
    actions: Vec<FuzzAction>,
}

fn arbitrary_packet<B: NestedPacketBuilder + std::fmt::Debug>(
    builder: B,
    u: &mut Unstructured<'_>,
) -> arbitrary::Result<Buf<Vec<u8>>> {
    let constraints = match builder.try_constraints() {
        Some(constraints) => constraints,
        None => return Err(arbitrary::Error::IncorrectFormat),
    };

    let body_len = std::cmp::min(
        std::cmp::max(u.arbitrary_len::<u8>()?, constraints.min_body_len()),
        constraints.max_body_len(),
    );
    let mut buffer = vec![0; body_len + constraints.header_len() + constraints.footer_len()];
    u.fill_buffer(&mut buffer[constraints.header_len()..(constraints.header_len() + body_len)])?;

    let bytes = Buf::new(buffer, constraints.header_len()..(constraints.header_len() + body_len))
        .encapsulate(builder)
        .serialize_vec_outer()
        .map_err(|(e, _): (_, Nested<Buf<Vec<_>>, B>)| match e {
            SerializeError::Alloc(e) => match e {},
            SerializeError::Mtu => arbitrary::Error::IncorrectFormat,
        })?
        .map_a(|buffer| Buf::new(buffer.as_ref().to_vec(), ..))
        .into_inner();
    Ok(bytes)
}

fn dispatch(ctx: &mut Ctx<DummyEventDispatcher>, device_id: DeviceId, action: FuzzAction) {
    use FuzzAction::*;
    match action {
        ReceiveFrame(ArbitraryFrame(frame_type, buf)) => {
            let _: FrameType = frame_type;
            crate::receive_frame(ctx, device_id, buf).expect("error receiving frame")
        }
        AdvanceTime(SmallDuration(duration)) => {
            let _: Vec<TimerId> = crate::testutil::run_for(ctx, duration);
        }
    }
}

#[::fuzz::fuzz]
pub(crate) fn single_device_arbitrary_packets(input: FuzzInput) {
    initialize_logging();
    let mut ctx = Ctx::with_default_state(DummyEventDispatcher::default());
    let FuzzInput { actions } = input;
    let device_id = ctx
        .state
        .add_ethernet_device(UnicastAddr::new(net_mac!("10:20:30:40:50:60")).unwrap(), 1500);
    crate::initialize_device(&mut ctx, device_id);
    actions.into_iter().for_each(|action| dispatch(&mut ctx, device_id, action));
}
