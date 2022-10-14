// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(dead_code, unused_imports, unused_macros)]

use core::{convert::TryInto as _, time::Duration};

use arbitrary::{Arbitrary, Unstructured};
use fuzz_util::Fuzzed;
use net_declare::net_mac;
use net_types::{
    ip::{IpAddress, Ipv4Addr, Ipv6Addr},
    UnicastAddr,
};
use packet::{
    serialize::{Buf, SerializeError},
    FragmentedBuffer, Nested, NestedPacketBuilder, Serializer,
};
use packet_formats::{
    ethernet::EthernetFrameBuilder,
    ip::{IpExt, IpPacketBuilder},
    ipv4::Ipv4PacketBuilder,
    ipv6::Ipv6PacketBuilder,
    tcp::TcpSegmentBuilder,
    udp::UdpPacketBuilder,
};

use crate::{
    context::testutil::{handle_timer_helper_with_sc_ref, FakeInstant, FakeTimerCtxExt},
    Ctx, DeviceId, TimerId,
};

mod print_on_panic {
    use core::fmt::{self, Display, Formatter};
    use std::sync::Mutex;

    use lazy_static::lazy_static;
    use log::{Log, Metadata, Record};

    lazy_static! {
        pub static ref PRINT_ON_PANIC: PrintOnPanicLog = PrintOnPanicLog::new();
        static ref PRINT_ON_PANIC_LOGGER: PrintOnPanicLogger = PrintOnPanicLogger;
    }

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
                let dispatched = core::mem::take(&mut *mutex.lock().unwrap());
                for o in dispatched.into_iter() {
                    println!("{}", o);
                }

                // Resume panicking normally.
                (*default_hook)(panic_info);
            }));
            Self(Mutex::new(Vec::new()))
        }

        /// Adds an entry to the log.
        fn record<T: Display>(&self, t: &T) {
            let Self(mutex) = self;
            mutex.lock().unwrap().push(t.to_string());
        }

        /// Clears the log.
        pub fn clear_log(&self) {
            let Self(mutex) = self;
            mutex.lock().unwrap().clear();
        }
    }

    struct PrintOnPanicLogger;

    impl Log for PrintOnPanicLogger {
        fn enabled(&self, _metadata: &Metadata<'_>) -> bool {
            true
        }

        fn log(&self, record: &Record<'_>) {
            struct DisplayRecord<'a, 'b>(&'a Record<'b>);
            impl<'a, 'b> Display for DisplayRecord<'a, 'b> {
                fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
                    let Self(record) = self;
                    write!(
                        f,
                        "[{}][{}] {}",
                        record.module_path().unwrap_or("_unknown_"),
                        record.level(),
                        record.args()
                    )
                }
            }
            PRINT_ON_PANIC.record(&DisplayRecord(record));
        }

        fn flush(&self) {}
    }

    /// Initializes the [`log`] crate so that all logs at or above the given
    /// severity level get written to [`PRINT_ON_PANIC`].
    ///
    /// When
    pub fn initialize_logging() {
        #[cfg(any(feature = "log_trace", feature = "log_debug", feature = "log_info"))]
        {
            const MAX_LOG_LEVEL: log::LevelFilter = if cfg!(feature = "log_trace") {
                log::LevelFilter::Trace
            } else if cfg!(feature = "log_debug") {
                log::LevelFilter::Debug
            } else {
                log::LevelFilter::Info
            };
            static LOGGER_ONCE: core::sync::atomic::AtomicBool =
                core::sync::atomic::AtomicBool::new(true);

            // This function gets called on every fuzz iteration, but we only need to set up logging the
            // first time.
            if LOGGER_ONCE.swap(false, core::sync::atomic::Ordering::AcqRel) {
                let logger: &PrintOnPanicLogger = &PRINT_ON_PANIC_LOGGER;
                log::set_logger(logger).expect("logging setup failed");
                println!("Saving {:?} logs in case of panic", MAX_LOG_LEVEL);
                log::set_max_level(MAX_LOG_LEVEL);
            };
        }
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
    Ipv6(IpFrameType),
}

#[derive(Copy, Clone, Debug, Arbitrary)]
enum IpFrameType {
    Raw,
    Udp,
    Tcp,
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
    fn arbitrary_buf<O: NestedPacketBuilder + core::fmt::Debug>(
        &self,
        outer: O,
        u: &mut Unstructured<'_>,
    ) -> arbitrary::Result<(Buf<Vec<u8>>, String)> {
        match self {
            EthernetFrameType::Raw => arbitrary_packet(outer, u),
            EthernetFrameType::Ipv4(ip_type) => {
                ip_type.arbitrary_buf::<Ipv4Addr, Ipv4PacketBuilder, _>(outer, u)
            }
            EthernetFrameType::Ipv6(ip_type) => {
                ip_type.arbitrary_buf::<Ipv6Addr, Ipv6PacketBuilder, _>(outer, u)
            }
        }
    }
}

impl IpFrameType {
    fn arbitrary_buf<
        'a,
        A: IpAddress,
        IPB: IpPacketBuilder<A::Version>,
        O: NestedPacketBuilder + core::fmt::Debug,
    >(
        &self,
        outer: O,
        u: &mut Unstructured<'a>,
    ) -> arbitrary::Result<(Buf<Vec<u8>>, String)>
    where
        A::Version: IpExt,
        Fuzzed<IPB>: Arbitrary<'a>,
    {
        match self {
            IpFrameType::Raw => arbitrary_packet(outer, u),
            IpFrameType::Udp => {
                let udp_in_ip = Fuzzed::<Nested<UdpPacketBuilder<A>, IPB>>::arbitrary(u)?.into();
                arbitrary_packet(udp_in_ip.encapsulate(outer), u)
            }
            IpFrameType::Tcp => {
                let tcp_in_ip = Fuzzed::<Nested<TcpSegmentBuilder<A>, IPB>>::arbitrary(u)?.into();
                arbitrary_packet(tcp_in_ip.encapsulate(outer), u)
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

impl core::fmt::Display for FuzzAction {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            FuzzAction::ReceiveFrame(ArbitraryFrame { frame_type, buf, description }) => write!(
                f,
                "Send {:?} frame with {} total bytes: {}",
                frame_type,
                buf.len(),
                description
            ),
            FuzzAction::AdvanceTime(SmallDuration(duration)) => {
                write!(f, "Advance time by {:?}", duration)
            }
        }
    }
}

fn arbitrary_packet<B: NestedPacketBuilder + core::fmt::Debug>(
    builder: B,
    u: &mut Unstructured<'_>,
) -> arbitrary::Result<(Buf<Vec<u8>>, String)> {
    let constraints = match builder.try_constraints() {
        Some(constraints) => constraints,
        None => return Err(arbitrary::Error::IncorrectFormat),
    };

    let body_len = core::cmp::min(
        core::cmp::max(u.arbitrary_len::<u8>()?, constraints.min_body_len()),
        constraints.max_body_len(),
    );

    // Generate a description that is used for logging. If logging is disabled,
    // the value here will never be printed. `String::new()` does not allocate,
    // so use that to save CPU and memory when the value would otherwise be
    // thrown away.
    let description = if log::log_enabled!(log::Level::Info) {
        format!("{:?} with body length {}", builder, body_len)
    } else {
        String::new()
    };

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

fn dispatch(
    Ctx { sync_ctx, non_sync_ctx }: &mut crate::testutil::FakeCtx,
    device_id: &DeviceId<FakeInstant>,
    action: FuzzAction,
) {
    use FuzzAction::*;
    match action {
        ReceiveFrame(ArbitraryFrame { frame_type: _, buf, description: _ }) => {
            crate::device::receive_frame(sync_ctx, non_sync_ctx, device_id, buf)
                .expect("error receiving frame")
        }
        AdvanceTime(SmallDuration(duration)) => {
            let _: Vec<TimerId<_>> = non_sync_ctx.trigger_timers_for(
                duration,
                handle_timer_helper_with_sc_ref(&*sync_ctx, crate::handle_timer),
            );
        }
    }
}

#[::fuzz::fuzz]
pub(crate) fn single_device_arbitrary_packets(input: FuzzInput) {
    print_on_panic::initialize_logging();

    let mut ctx = crate::testutil::FakeCtx::default();
    let FuzzInput { actions } = input;

    let Ctx { sync_ctx, non_sync_ctx } = &mut ctx;
    let device_id = crate::device::add_ethernet_device(
        sync_ctx,
        non_sync_ctx,
        UnicastAddr::new(net_mac!("10:20:30:40:50:60")).unwrap(),
        1500,
    );
    crate::device::testutil::enable_device(sync_ctx, non_sync_ctx, &device_id);

    log::info!("Processing {} actions", actions.len());
    for action in actions {
        log::info!("{}", action);
        dispatch(&mut ctx, &device_id, action);
    }

    // No panic occurred, so clear the log for the next run.
    print_on_panic::PRINT_ON_PANIC.clear_log();
}
