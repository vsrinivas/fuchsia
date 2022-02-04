// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(dead_code, unused_imports, unused_macros)]

use core::time::Duration;
use std::convert::TryInto as _;

use crate::{testutil::DummyEventDispatcher, Ctx, DeviceId, TimerId};
use arbitrary::{Arbitrary, Unstructured};
use fuzz_util::Fuzzed;
use net_declare::net_mac;
use net_types::{
    ip::{IpAddress, Ipv4Addr},
    UnicastAddr,
};
use packet::{
    serialize::{Buf, SerializeError},
    FragmentedBuffer, Nested, NestedPacketBuilder, Serializer,
};
use packet_formats::{
    ethernet::EthernetFrameBuilder, ip::IpPacketBuilder, ipv4::Ipv4PacketBuilder,
    udp::UdpPacketBuilder,
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

mod print_on_panic {
    use lazy_static::lazy_static;
    use std::sync::Mutex;

    /// A simple log whose contents get printed to stdout on panic.
    ///
    /// The singleton instance of this can be obtained via the static singleton
    /// [`PRINT_ON_PANIC`].
    pub struct PrintOnPanicLog(Mutex<Vec<String>>);

    impl PrintOnPanicLog {
        /// Constructs the singleton log instance.
        fn new() -> Self {
            let default_hook = std::panic::take_hook();

            std::panic::set_hook(Box::new(move |panic_info| {
                let Self(mutex): &PrintOnPanicLog = &PRINT_ON_PANIC;
                let dispatched = std::mem::take(&mut *mutex.lock().unwrap());
                if dispatched.is_empty() {
                    println!("Processed: [none]");
                } else {
                    println!("Processed {} items", dispatched.len());
                    for o in dispatched.into_iter() {
                        println!("{}", o);
                    }
                }

                // Resume panicking normally.
                (*default_hook)(panic_info);
            }));
            Self(Mutex::new(Vec::new()))
        }

        /// Adds an entry to the log.
        pub fn record<T: std::fmt::Display>(&self, t: &T) {
            let Self(mutex) = self;
            mutex.lock().unwrap().push(t.to_string());
        }

        /// Clears the log.
        pub fn clear_log(&self) {
            let Self(mutex) = self;
            mutex.lock().unwrap().clear();
        }
    }

    lazy_static! {
        pub static ref PRINT_ON_PANIC: PrintOnPanicLog = PrintOnPanicLog::new();
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
    fn arbitrary_buf(&self, u: &mut Unstructured<'_>) -> arbitrary::Result<(Buf<Vec<u8>>, String)> {
        match self {
            FrameType::Raw => Ok((Buf::new(u.arbitrary()?, ..), "[raw]".into())),
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
    ) -> arbitrary::Result<(Buf<Vec<u8>>, String)> {
        match self {
            EthernetFrameType::Raw => arbitrary_packet(outer, u),
            EthernetFrameType::Ipv4(ip_type) => {
                ip_type.arbitrary_buf::<Ipv4Addr, Ipv4PacketBuilder, _>(outer, u)
            }
        }
    }
}

impl IpFrameType {
    fn arbitrary_buf<
        'a,
        A: IpAddress,
        IPB: IpPacketBuilder<A::Version>,
        O: NestedPacketBuilder + std::fmt::Debug,
    >(
        &self,
        outer: O,
        u: &mut Unstructured<'a>,
    ) -> arbitrary::Result<(Buf<Vec<u8>>, String)>
    where
        Fuzzed<IPB>: Arbitrary<'a>,
    {
        match self {
            IpFrameType::Raw => arbitrary_packet(outer, u),
            IpFrameType::Udp => {
                let udp_in_ip = Fuzzed::<Nested<UdpPacketBuilder<A>, IPB>>::arbitrary(u)?.into();
                arbitrary_packet(udp_in_ip.encapsulate(outer), u)
            }
        }
    }
}

struct ArbitraryFrame {
    frame_type: FrameType,
    buf: Buf<Vec<u8>>,
    description: String,
}

impl<'a> Arbitrary<'a> for ArbitraryFrame {
    fn arbitrary(u: &mut Unstructured<'a>) -> arbitrary::Result<Self> {
        let frame_type = u.arbitrary::<FrameType>()?;
        let (buf, description) = frame_type.arbitrary_buf(u)?;
        Ok(Self { frame_type, buf, description })
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

impl std::fmt::Display for FuzzAction {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            FuzzAction::ReceiveFrame(ArbitraryFrame { frame_type, buf, description }) => f
                .write_fmt(format_args!(
                    "Send {:?} frame with {} total bytes: {}",
                    frame_type,
                    buf.len(),
                    description
                )),
            FuzzAction::AdvanceTime(SmallDuration(duration)) => {
                f.write_fmt(format_args!("Advance time by {:?}", duration))
            }
        }
    }
}

fn arbitrary_packet<B: NestedPacketBuilder + std::fmt::Debug>(
    builder: B,
    u: &mut Unstructured<'_>,
) -> arbitrary::Result<(Buf<Vec<u8>>, String)> {
    let constraints = match builder.try_constraints() {
        Some(constraints) => constraints,
        None => return Err(arbitrary::Error::IncorrectFormat),
    };

    let body_len = std::cmp::min(
        std::cmp::max(u.arbitrary_len::<u8>()?, constraints.min_body_len()),
        constraints.max_body_len(),
    );

    let description = format!("{:?} with body length {}", builder, body_len);

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
    Ok((bytes, description))
}

fn dispatch(ctx: &mut Ctx<DummyEventDispatcher>, device_id: DeviceId, action: FuzzAction) {
    use FuzzAction::*;
    match action {
        ReceiveFrame(ArbitraryFrame { frame_type: _, buf, description: _ }) => {
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

    for action in actions {
        print_on_panic::PRINT_ON_PANIC.record(&action);
        dispatch(&mut ctx, device_id, action);
    }

    // No panic occurred, so clear the log for the next run.
    print_on_panic::PRINT_ON_PANIC.clear_log();
}
