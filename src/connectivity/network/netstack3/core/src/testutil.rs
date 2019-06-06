// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Testing-related utilities.

use std::collections::{BinaryHeap, HashMap};
use std::hash::Hash;
use std::sync::Once;
use std::time::{Duration, Instant};

use byteorder::{ByteOrder, NativeEndian};
use log::debug;
use packet::{ParsablePacket, ParseBuffer};
use rand::SeedableRng;
use rand_xorshift::XorShiftRng;

use crate::device::ethernet::{EtherType, Mac};
use crate::device::{DeviceId, DeviceLayerEventDispatcher};
use crate::error::{IpParseResult, ParseError, ParseResult};
use crate::ip::{
    AddrSubnet, Ip, IpAddr, IpAddress, IpExt, IpPacket, IpProto, Ipv4Addr, Ipv6Addr, Subnet,
    SubnetEither, IPV6_MIN_MTU,
};
use crate::transport::udp::UdpEventDispatcher;
use crate::transport::TransportLayerEventDispatcher;
use crate::wire::ethernet::EthernetFrame;
use crate::wire::icmp::{IcmpMessage, IcmpPacket, IcmpParseArgs};
use crate::{handle_timeout, Context, EventDispatcher, StackStateBuilder, TimerId};

use specialize_ip_macro::specialize_ip_address;

/// Create a new deterministic RNG from a seed.
pub(crate) fn new_rng(mut seed: u64) -> XorShiftRng {
    if seed == 0 {
        // XorShiftRng can't take 0 seeds
        seed = 1;
    }
    let mut bytes = [0; 16];
    NativeEndian::write_u32(&mut bytes[0..4], seed as u32);
    NativeEndian::write_u32(&mut bytes[4..8], (seed >> 32) as u32);
    NativeEndian::write_u32(&mut bytes[8..12], seed as u32);
    NativeEndian::write_u32(&mut bytes[12..16], (seed >> 32) as u32);
    XorShiftRng::from_seed(bytes)
}

#[derive(Default, Debug)]
pub(crate) struct TestCounters {
    data: HashMap<String, usize>,
}

impl TestCounters {
    pub(crate) fn increment(&mut self, key: &str) {
        *(self.data.entry(key.to_string()).or_insert(0)) += 1;
    }

    pub(crate) fn get(&self, key: &str) -> &usize {
        self.data.get(key).unwrap_or(&0)
    }
}

/// log::Log implementation that uses stdout.
///
/// Useful when debugging tests.
struct Logger;

impl log::Log for Logger {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        true
    }

    fn log(&self, record: &log::Record) {
        println!("{}", record.args())
    }

    fn flush(&self) {}
}

static LOGGER: Logger = Logger;

static LOGGER_ONCE: Once = Once::new();

/// Install a logger for tests.
///
/// Call this method at the beginning of the test for which logging is desired.
/// This function sets global program state, so all tests that run after this
/// function is called will use the logger.
pub(crate) fn set_logger_for_test() {
    // log::set_logger will panic if called multiple times; using a Once makes
    // set_logger_for_test idempotent
    LOGGER_ONCE.call_once(|| {
        log::set_logger(&LOGGER).unwrap();
        log::set_max_level(log::LevelFilter::Trace);
    })
}

/// Skip current (fake) time forward to trigger the next timer event.
///
/// Returns true if a timer was triggered, false if there were no timers waiting
/// to be triggered.
pub(crate) fn trigger_next_timer(ctx: &mut Context<DummyEventDispatcher>) -> bool {
    match ctx.dispatcher.timer_events.pop() {
        Some(InstantAndData(t, id)) => {
            ctx.dispatcher.current_time = t;
            handle_timeout(ctx, id);
            true
        }
        None => false,
    }
}

/// Trigger timer events until`f` callback returns true or passes the max
/// number of iterations.
///
/// `trigger_timers_until` always calls `f` on the first timer event, as the
/// timer_events is dynamically updated. As soon as `f` returns true or
/// 1,000,000 timer events have been triggered, `trigger_timers_until` will
/// exit.
///
/// Please note, the caller is expected to pass in an `f` which could return
/// true to exit `trigger_timer_until`. 1,000,000 limit is set to avoid an
/// endless loop.
pub(crate) fn trigger_timers_until<F: Fn(&TimerId) -> bool>(
    ctx: &mut Context<DummyEventDispatcher>,
    f: F,
) {
    for _ in 0..1_000_000 {
        let InstantAndData(t, id) = if let Some(t) = ctx.dispatcher.timer_events.pop() {
            t
        } else {
            return;
        };

        ctx.dispatcher.current_time = t;
        handle_timeout(ctx, id);
        if f(&id) {
            break;
        }
    }
}

/// Parse an ethernet frame.
///
/// `parse_ethernet_frame` parses an ethernet frame, returning the body along
/// with some important header fields.
pub(crate) fn parse_ethernet_frame(
    mut buf: &[u8],
) -> ParseResult<(&[u8], Mac, Mac, Option<EtherType>)> {
    let frame = (&mut buf).parse::<EthernetFrame<_>>()?;
    let src_mac = frame.src_mac();
    let dst_mac = frame.dst_mac();
    let ethertype = frame.ethertype();
    Ok((buf, src_mac, dst_mac, ethertype))
}

/// Parse an IP packet.
///
/// `parse_ip_packet` parses an IP packet, returning the body along with some
/// important header fields.
#[allow(clippy::type_complexity)]
pub(crate) fn parse_ip_packet<I: Ip>(
    mut buf: &[u8],
) -> IpParseResult<I, (&[u8], I::Addr, I::Addr, IpProto)> {
    let packet = (&mut buf).parse::<<I as IpExt<_>>::Packet>()?;
    let src_ip = packet.src_ip();
    let dst_ip = packet.dst_ip();
    let proto = packet.proto();
    // Because the packet type here is generic, Rust doesn't know that it
    // doesn't implement Drop, and so it doesn't know that it's safe to drop as
    // soon as it's no longer used and allow buf to no longer be borrowed on the
    // next line. It works fine in parse_ethernet_frame because EthernetFrame is
    // a concrete type which Rust knows doesn't implement Drop.
    std::mem::drop(packet);
    Ok((buf, src_ip, dst_ip, proto))
}

/// Parse an ICMP packet.
///
/// `parse_icmp_packet` parses an ICMP packet, returning the body along with
/// some important fields. Before returning, it invokes the callback `f` on the
/// parsed packet.
pub(crate) fn parse_icmp_packet<
    I: Ip,
    C,
    M: for<'a> IcmpMessage<I, &'a [u8], Code = C>,
    F: for<'a> Fn(&IcmpPacket<I, &'a [u8], M>),
>(
    mut buf: &[u8],
    src_ip: I::Addr,
    dst_ip: I::Addr,
    f: F,
) -> ParseResult<(M, C)>
where
    for<'a> IcmpPacket<I, &'a [u8], M>:
        ParsablePacket<&'a [u8], IcmpParseArgs<I::Addr>, Error = ParseError>,
{
    let packet =
        (&mut buf).parse_with::<_, IcmpPacket<I, _, M>>(IcmpParseArgs::new(src_ip, dst_ip))?;
    let message = *packet.message();
    let code = packet.code();
    f(&packet);
    Ok((message, code))
}

/// Parse an IP packet in an Ethernet frame.
///
/// `parse_ip_packet_in_ethernet_frame` parses an IP packet in an Ethernet
/// frame, returning the body of the IP packet along with some important fields
/// from both the IP and Ethernet headers.
#[allow(clippy::type_complexity)]
pub(crate) fn parse_ip_packet_in_ethernet_frame<I: Ip>(
    buf: &[u8],
) -> IpParseResult<I, (&[u8], Mac, Mac, I::Addr, I::Addr, IpProto)> {
    use crate::device::ethernet::EthernetIpExt;
    let (body, src_mac, dst_mac, ethertype) = parse_ethernet_frame(buf)?;
    if ethertype != Some(I::ETHER_TYPE) {
        debug!("unexpected ethertype: {:?}", ethertype);
        return Err(ParseError::NotExpected.into());
    }

    let (body, src_ip, dst_ip, proto) = parse_ip_packet::<I>(body)?;
    Ok((body, src_mac, dst_mac, src_ip, dst_ip, proto))
}

/// Parse an ICMP packet in an IP packet in an Ethernet frame.
///
/// `parse_icmp_packet_in_ip_packet_in_ethernet_frame` parses an ICMP packet in
/// an IP packet in an Ethernet frame, returning the message and code from the
/// ICMP packet along with some important fields from both the IP and Ethernet
/// headers. Before returning, it invokes the callback `f` on the parsed packet.
#[allow(clippy::type_complexity)]
pub(crate) fn parse_icmp_packet_in_ip_packet_in_ethernet_frame<
    I: Ip,
    C,
    M: for<'a> IcmpMessage<I, &'a [u8], Code = C>,
    F: for<'a> Fn(&IcmpPacket<I, &'a [u8], M>),
>(
    mut buf: &[u8],
    f: F,
) -> IpParseResult<I, (Mac, Mac, I::Addr, I::Addr, M, C)>
where
    for<'a> IcmpPacket<I, &'a [u8], M>:
        ParsablePacket<&'a [u8], IcmpParseArgs<I::Addr>, Error = ParseError>,
{
    use crate::wire::icmp::IcmpIpExt;

    let (mut body, src_mac, dst_mac, src_ip, dst_ip, proto) =
        parse_ip_packet_in_ethernet_frame::<I>(buf)?;
    if proto != <I as IcmpIpExt<&[u8]>>::IP_PROTO {
        debug!("unexpected IP protocol: {} (wanted {})", proto, <I as IcmpIpExt<&[u8]>>::IP_PROTO);
        return Err(ParseError::NotExpected.into());
    }
    let (message, code) = parse_icmp_packet(body, src_ip, dst_ip, f)?;
    Ok((src_mac, dst_mac, src_ip, dst_ip, message, code))
}

/// A configuration for a simple network.
///
/// `DummyEventDispatcherConfig` describes a simple network with two IP hosts
/// - one remote and one local - both on the same Ethernet network.
pub(crate) struct DummyEventDispatcherConfig<A: IpAddress> {
    /// The subnet of the local Ethernet network.
    pub(crate) subnet: Subnet<A>,
    /// The IP address of our interface to the local network (must be in
    /// subnet).
    pub(crate) local_ip: A,
    /// The MAC address of our interface to the local network.
    pub(crate) local_mac: Mac,
    /// The remote host's IP address (must be in subnet if provided).
    pub(crate) remote_ip: A,
    /// The remote host's MAC address.
    pub(crate) remote_mac: Mac,
}

/// A `DummyEventDispatcherConfig` with reasonable values for an IPv4 network.
pub(crate) const DUMMY_CONFIG_V4: DummyEventDispatcherConfig<Ipv4Addr> =
    DummyEventDispatcherConfig {
        subnet: unsafe { Subnet::new_unchecked(Ipv4Addr::new([192, 168, 0, 0]), 16) },
        local_ip: Ipv4Addr::new([192, 168, 0, 1]),
        local_mac: Mac::new([0, 1, 2, 3, 4, 5]),
        remote_ip: Ipv4Addr::new([192, 168, 0, 2]),
        remote_mac: Mac::new([6, 7, 8, 9, 10, 11]),
    };

/// A `DummyEventDispatcherConfig` with reasonable values for an IPv6 network.
pub(crate) const DUMMY_CONFIG_V6: DummyEventDispatcherConfig<Ipv6Addr> =
    DummyEventDispatcherConfig {
        subnet: unsafe {
            Subnet::new_unchecked(
                Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 0]),
                112,
            )
        },
        local_ip: Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 1]),
        local_mac: Mac::new([0, 1, 2, 3, 4, 5]),
        remote_ip: Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 2]),
        remote_mac: Mac::new([6, 7, 8, 9, 10, 11]),
    };

impl<A: IpAddress> DummyEventDispatcherConfig<A> {
    /// Creates a copy of `self` with all the remote and local fields reversed.
    pub(crate) fn swap(&self) -> Self {
        Self {
            subnet: self.subnet,
            local_ip: self.remote_ip,
            local_mac: self.remote_mac,
            remote_ip: self.local_ip,
            remote_mac: self.local_mac,
        }
    }
}

/// A builder for `DummyEventDispatcher`s.
///
/// A `DummyEventDispatcherBuilder` is capable of storing the configuration of a
/// network stack including forwarding table entries, devices and their assigned
/// IP addresses, ARP table entries, etc. It can be built using `build`,
/// producing a `Context<DummyEventDispatcher>` with all of the appropriate
/// state configured.
#[derive(Clone, Default)]
pub(crate) struct DummyEventDispatcherBuilder {
    devices: Vec<(Mac, Option<(IpAddr, SubnetEither)>)>,
    arp_table_entries: Vec<(usize, Ipv4Addr, Mac)>,
    ndp_table_entries: Vec<(usize, Ipv6Addr, Mac)>,
    // usize refers to index into devices Vec
    device_routes: Vec<(SubnetEither, usize)>,
    routes: Vec<(SubnetEither, IpAddr)>,
}

impl DummyEventDispatcherBuilder {
    /// Construct a `DummyEventDispatcherBuilder` from a `DummyEventDispatcherConfig`.
    #[specialize_ip_address]
    pub(crate) fn from_config<A: IpAddress>(
        cfg: DummyEventDispatcherConfig<A>,
    ) -> DummyEventDispatcherBuilder {
        assert!(cfg.subnet.contains(cfg.local_ip));
        assert!(cfg.subnet.contains(cfg.remote_ip));

        let mut builder = DummyEventDispatcherBuilder::default();
        builder.devices.push((cfg.local_mac, Some((cfg.local_ip.into(), cfg.subnet.into()))));

        #[ipv4addr]
        builder.arp_table_entries.push((0, cfg.remote_ip, cfg.remote_mac));
        #[ipv6addr]
        builder.ndp_table_entries.push((0, cfg.remote_ip, cfg.remote_mac));

        // even with fixed ipv4 address we can have ipv6 link local addresses
        // pre-cached.
        builder.ndp_table_entries.push((
            0,
            cfg.remote_mac.to_ipv6_link_local(None),
            cfg.remote_mac,
        ));

        builder.device_routes.push((cfg.subnet.into(), 0));
        builder
    }

    /// Add a device.
    ///
    /// `add_device` returns a key which can be used to refer to the device in
    /// future calls to `add_arp_table_entry` and `add_device_route`.
    pub(crate) fn add_device(&mut self, mac: Mac) -> usize {
        let idx = self.devices.len();
        self.devices.push((mac, None));
        idx
    }

    /// Add a device with an associated IP address.
    ///
    /// `add_device_with_ip` is like `add_device`, except that it takes an
    /// associated IP address and subnet to assign to the device.
    pub(crate) fn add_device_with_ip<A: IpAddress>(
        &mut self,
        mac: Mac,
        ip: A,
        subnet: Subnet<A>,
    ) -> usize {
        let idx = self.devices.len();
        self.devices.push((mac, Some((ip.into(), subnet.into()))));
        idx
    }

    /// Add an ARP table entry for a device's ARP table.
    pub(crate) fn add_arp_table_entry(&mut self, device: usize, ip: Ipv4Addr, mac: Mac) {
        self.arp_table_entries.push((device, ip, mac));
    }

    /// Add an NDP table entry for a device's NDP table.
    pub(crate) fn add_ndp_table_entry(&mut self, device: usize, ip: Ipv6Addr, mac: Mac) {
        self.ndp_table_entries.push((device, ip, mac));
    }

    /// Add a route to the forwarding table.
    pub(crate) fn add_route<A: IpAddress>(&mut self, subnet: Subnet<A>, next_hop: A) {
        self.routes.push((subnet.into(), next_hop.into()));
    }

    /// Add a device route to the forwarding table.
    pub(crate) fn add_device_route<A: IpAddress>(&mut self, subnet: Subnet<A>, device: usize) {
        self.device_routes.push((subnet.into(), device));
    }

    /// Build a `Context` from the present configuration with a default state
    /// and dispatcher.
    ///
    /// `b.build()` is equivalent to `b.build_with(StackStateBuilder::default(),
    /// D::default())`.
    pub(crate) fn build<D: EventDispatcher + Default>(self) -> Context<D> {
        self.build_with(StackStateBuilder::default(), D::default())
    }

    /// Build a `Context` from the present configuration with a caller-provided
    /// dispatcher and `StackStateBuilder`.
    pub(crate) fn build_with<D: EventDispatcher>(
        self,
        state_builder: StackStateBuilder,
        dispatcher: D,
    ) -> Context<D> {
        let mut ctx = Context::new(state_builder.build(), dispatcher);

        let DummyEventDispatcherBuilder {
            devices,
            arp_table_entries,
            ndp_table_entries,
            device_routes,
            routes,
        } = self;
        let mut idx_to_device_id =
            HashMap::<_, _, std::collections::hash_map::RandomState>::default();
        for (idx, (mac, ip_subnet)) in devices.into_iter().enumerate() {
            let id = ctx.state_mut().add_ethernet_device(mac, IPV6_MIN_MTU);
            idx_to_device_id.insert(idx, id);
            match ip_subnet {
                Some((IpAddr::V4(ip), SubnetEither::V4(subnet))) => {
                    let addr_sub = AddrSubnet::new(ip, subnet.prefix()).unwrap();
                    crate::device::set_ip_addr_subnet(&mut ctx, id, addr_sub);
                }
                Some((IpAddr::V6(ip), SubnetEither::V6(subnet))) => {
                    let addr_sub = AddrSubnet::new(ip, subnet.prefix()).unwrap();
                    crate::device::set_ip_addr_subnet(&mut ctx, id, addr_sub);
                }
                None => {}
                _ => unreachable!(),
            }
        }
        for (idx, ip, mac) in arp_table_entries {
            let device = *idx_to_device_id.get(&idx).unwrap();
            crate::device::ethernet::insert_static_arp_table_entry(&mut ctx, device.id(), ip, mac);
        }
        for (idx, ip, mac) in ndp_table_entries {
            let device = *idx_to_device_id.get(&idx).unwrap();
            crate::device::ethernet::insert_ndp_table_entry(&mut ctx, device.id(), ip, mac);
        }
        for (subnet, idx) in device_routes {
            let device = *idx_to_device_id.get(&idx).unwrap();
            match subnet {
                SubnetEither::V4(subnet) => crate::ip::add_device_route(&mut ctx, subnet, device),
                SubnetEither::V6(subnet) => crate::ip::add_device_route(&mut ctx, subnet, device),
            };
        }
        for (subnet, next_hop) in routes {
            match (subnet, next_hop) {
                (SubnetEither::V4(subnet), IpAddr::V4(next_hop)) => {
                    crate::ip::add_route(&mut ctx, subnet, next_hop)
                }
                (SubnetEither::V6(subnet), IpAddr::V6(next_hop)) => {
                    crate::ip::add_route(&mut ctx, subnet, next_hop)
                }
                _ => unreachable!(),
            };
        }

        ctx
    }
}

/// Represents arbitrary data of type `D` attached to an `Instant`.
///
/// `InstantAndData` implements `Ord` and `Eq` to be used in a `BinaryHeap`
/// and ordered by `Instant`.
struct InstantAndData<D>(Instant, D);

impl<D> InstantAndData<D> {
    fn new(time: Instant, data: D) -> Self {
        Self(time, data)
    }
}

impl<D> Eq for InstantAndData<D> {}

impl<D> PartialEq for InstantAndData<D> {
    fn eq(&self, other: &Self) -> bool {
        self.0 == other.0
    }
}

impl<D> Ord for InstantAndData<D> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        other.0.cmp(&self.0)
    }
}

impl<D> PartialOrd for InstantAndData<D> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

type PendingTimer = InstantAndData<TimerId>;

/// A dummy `EventDispatcher` used for testing.
///
/// A `DummyEventDispatcher` implements the `EventDispatcher` interface for
/// testing purposes. It provides facilities to inspect the history of what
/// events have been emitted to the system.
pub(crate) struct DummyEventDispatcher {
    frames_sent: Vec<(DeviceId, Vec<u8>)>,
    timer_events: BinaryHeap<PendingTimer>,
    current_time: Instant,
}

impl DummyEventDispatcher {
    pub(crate) fn frames_sent(&self) -> &[(DeviceId, Vec<u8>)] {
        &self.frames_sent
    }

    /// Get an ordered list of all scheduled timer events
    pub(crate) fn timer_events(&self) -> impl Iterator<Item = (&'_ Instant, &'_ TimerId)> {
        self.timer_events.iter().map(|t| (&t.0, &t.1))
    }

    /// Get the current (fake) time
    pub(crate) fn current_time(&self) -> Instant {
        self.current_time
    }

    /// Forwards all the frames kept in the `self` to another context.
    ///
    /// This function  drains all the events in `self` and moves the data to
    /// another context. `mapper` is used to map a `DeviceId` from the current
    /// context to a `DeviceId` in `other`.
    pub(crate) fn forward_frames<D: EventDispatcher, F>(
        &mut self,
        other: &mut Context<D>,
        mapper: F,
    ) where
        F: Fn(DeviceId) -> DeviceId,
    {
        for (device_id, mut data) in self.frames_sent.drain(..) {
            crate::receive_frame(other, mapper(device_id), &mut data);
        }
    }
}

impl Default for DummyEventDispatcher {
    fn default() -> DummyEventDispatcher {
        DummyEventDispatcher {
            frames_sent: vec![],
            timer_events: BinaryHeap::new(),
            current_time: Instant::now(),
        }
    }
}

impl UdpEventDispatcher for DummyEventDispatcher {
    type UdpConn = ();
    type UdpListener = ();
}

impl TransportLayerEventDispatcher for DummyEventDispatcher {}

impl DeviceLayerEventDispatcher for DummyEventDispatcher {
    fn send_frame(&mut self, device: DeviceId, frame: &[u8]) {
        self.frames_sent.push((device, frame.to_vec()));
    }
}

impl EventDispatcher for DummyEventDispatcher {
    fn schedule_timeout(&mut self, duration: Duration, id: TimerId) -> Option<Instant> {
        self.schedule_timeout_instant(self.current_time + duration, id)
    }

    fn schedule_timeout_instant(&mut self, time: Instant, id: TimerId) -> Option<Instant> {
        let ret = self.cancel_timeout(id);
        self.timer_events.push(PendingTimer::new(time, id));
        ret
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<Instant> {
        let mut r: Option<Instant> = None;
        // NOTE(brunodalbo): cancelling timeouts can be made a faster than this
        //  if we kept two data structures and TimerId was Hashable.
        self.timer_events = self
            .timer_events
            .drain()
            .filter(|t| {
                if t.1 == id {
                    r = Some(t.0);
                    false
                } else {
                    true
                }
            })
            .collect::<Vec<_>>()
            .into();
        r
    }
}

#[derive(Debug)]
struct PendingFrameData<N> {
    data: Vec<u8>,
    dst_context: N,
    dst_device: DeviceId,
}

type PendingFrame<N> = InstantAndData<PendingFrameData<N>>;

/// A dummy network, composed of many `Context`s backed by
/// `DummyEventDispatcher`s.
///
/// Provides a quick utility to have many contexts keyed by `N` that can
/// exchange frames between their interfaces, which are mapped by `mapper`.
/// `mapper` also provides the option to return a `Duration` parameter that is
/// interpreted as a delivery latency for a given packet.
pub(crate) struct DummyNetwork<
    N: Eq + Hash + Clone,
    F: Fn(&N, DeviceId) -> (N, DeviceId, Option<Duration>),
> {
    contexts: HashMap<N, Context<DummyEventDispatcher>>,
    mapper: F,
    current_time: Instant,
    pending_frames: BinaryHeap<PendingFrame<N>>,
}

/// The result of a single step in a `DummyNetwork`
#[derive(Debug)]
pub(crate) struct StepResult {
    time_delta: Duration,
    timers_fired: usize,
    frames_sent: usize,
}

impl StepResult {
    fn new(time_delta: Duration, timers_fired: usize, frames_sent: usize) -> Self {
        Self { time_delta, timers_fired, frames_sent }
    }

    fn new_idle() -> Self {
        Self::new(Duration::from_millis(0), 0, 0)
    }

    /// Returns the time jump in the last step.
    pub(crate) fn time_delta(&self) -> Duration {
        self.time_delta
    }

    /// Returns `true` if the last step did not perform any operations.
    pub(crate) fn is_idle(&self) -> bool {
        return self.timers_fired == 0 && self.frames_sent == 0;
    }

    /// Returns the number of frames dispatched to their destinations in the
    /// last step.
    pub(crate) fn frames_sent(&self) -> usize {
        self.frames_sent
    }

    /// Returns the number of timers fired in the last step.
    pub(crate) fn timers_fired(&self) -> usize {
        self.timers_fired
    }
}

/// Error type that marks that one of the `run_until` family of functions
/// reached a maximum number of iterations.
#[derive(Debug)]
pub(crate) struct LoopLimitReachedError;

impl<N, F> DummyNetwork<N, F>
where
    N: Eq + Hash + Clone + std::fmt::Debug,
    F: Fn(&N, DeviceId) -> (N, DeviceId, Option<Duration>),
{
    /// Creates a new `DummyNetwork`.
    ///
    /// Creates a new `DummyNetwork` with the collection of `Context`s in
    /// `contexts`. `Context`s are named by type parameter `N`. `mapper`
    /// is used to route frames from one pair of (named `Context`, `DeviceId`)
    /// to another.
    ///
    /// # Panics
    ///
    /// `mapper` must map to a valid name, otherwise calls to `step` will panic.
    ///
    /// Calls to `new` will panic if given a `Context` with timer events.
    /// `Context`s given to `DummyNetwork` **must not** have any timer events
    /// already attached to them, because `DummyNetwork` maintains all the
    /// internal timers in dispatchers in sync to enable synchronous simulation
    /// steps.
    pub(crate) fn new<I: Iterator<Item = (N, Context<DummyEventDispatcher>)>>(
        contexts: I,
        mapper: F,
    ) -> Self {
        let mut ret = Self {
            contexts: contexts.collect(),
            mapper,
            current_time: Instant::now(),
            pending_frames: BinaryHeap::new(),
        };

        // We can't guarantee that all contexts are safely running their timers
        // together if we receive a context with any timers already set.
        assert!(
            !ret.contexts.iter().any(|(n, ctx)| { !ctx.dispatcher.timer_events.is_empty() }),
            "can't start network with contexts that already have timers set"
        );

        // synchronize all dispatchers' current time to the same value:
        for (_, ctx) in ret.contexts.iter_mut() {
            ctx.dispatcher.current_time = ret.current_time;
        }

        ret
    }

    /// Retrieves a `Context` named `context`.
    pub(crate) fn context<K: Into<N>>(&mut self, context: K) -> &mut Context<DummyEventDispatcher> {
        self.contexts.get_mut(&context.into()).unwrap()
    }

    /// Performs a single step in network simulation.
    ///
    /// `step` performs a single logical step in the collection of `Context`s
    /// held by this `DummyNetwork`. A single step consists of the following
    /// operations:
    ///
    /// - All pending frames, kept in `frames_sent` of `DummyEventDispatcher`
    /// are mapped to their destination `Context`/`DeviceId` pairs and moved to
    /// an internal collection of pending frames.
    /// - The collection of pending timers and scheduled frames is inspected and
    /// a simulation time step is retrieved, which will cause a next event
    /// to trigger. The simulation time is updated to the new time.
    /// - All scheduled frames whose deadline is less than or equal to the new
    /// simulation time are sent to their destinations.
    /// - All timer events whose deadline is less than or equal to the new
    /// simulation time are fired.
    ///
    /// If any new events are created during the operation of frames or timers,
    /// they **will not** be taken into account in the current `step`. That is,
    /// `step` collects all the pending events before dispatching them, ensuring
    /// that an infinite loop can't be created as a side effect of calling
    /// `step`.
    ///
    /// The return value of `step` indicates which of the operations were
    /// performed.
    ///
    /// # Panics
    ///
    /// If `DummyNetwork` was set up with a bad `mapper`, calls to `step` may
    /// panic when trying to route frames to their `Context`/`DeviceId`
    /// destinations.
    pub(crate) fn step(&mut self) -> StepResult {
        self.collect_frames();

        let next_step = if let Some(t) = self.next_step() {
            t
        } else {
            return StepResult::new_idle();
        };

        // this assertion holds the contract that `next_step` does not return
        // a time in the past.
        assert!(next_step >= self.current_time);
        let mut ret = StepResult::new(next_step.duration_since(self.current_time), 0, 0);
        // move time forward:
        self.current_time = next_step;
        for (_, ctx) in self.contexts.iter_mut() {
            ctx.dispatcher.current_time = next_step;
        }

        // dispatch all pending frames:
        while let Some(InstantAndData(t, _)) = self.pending_frames.peek() {
            // TODO(brunodalbo): remove this break once let_chains is stable
            if *t > self.current_time {
                break;
            }
            // we can unwrap because we just peeked.
            let mut frame = self.pending_frames.pop().unwrap().1;
            crate::receive_frame(
                self.context(frame.dst_context),
                frame.dst_device,
                &mut frame.data,
            );
            ret.frames_sent += 1;
        }

        // dispatch all pending timers.
        for (n, ctx) in self.contexts.iter_mut() {
            // We have to collect the timers before dispatching them, to avoid
            // an infinite loop in case handle_timeout schedules another timer for
            // the same or older Instant.
            let mut timers = Vec::<TimerId>::new();
            while let Some(InstantAndData(t, id)) = ctx.dispatcher.timer_events.peek() {
                // TODO(brunodalbo): remove this break once let_chains is stable
                if *t > self.current_time {
                    break;
                }
                timers.push(*id);
                ctx.dispatcher.timer_events.pop();
            }

            for t in timers {
                crate::handle_timeout(ctx, t);
                ret.timers_fired += 1;
            }
        }

        ret
    }

    /// Collects all queued frames.
    ///
    /// Collects all pending frames and schedules them for delivery to the
    /// destination `Context`/`DeviceId` based on the result of `mapper`. The
    /// collected frames are queued for dispatching in the `DummyNetwork`,
    /// ordered by their scheduled delivery time given by the latency result
    /// provided by `mapper`.
    fn collect_frames(&mut self) {
        let all_frames: Vec<(N, Vec<(DeviceId, Vec<u8>)>)> = self
            .contexts
            .iter_mut()
            .filter_map(|(n, ctx)| {
                if ctx.dispatcher.frames_sent.is_empty() {
                    None
                } else {
                    Some((n.clone(), ctx.dispatcher.frames_sent.drain(..).collect()))
                }
            })
            .collect();

        for (n, frames) in all_frames.into_iter() {
            for (device_id, mut frame) in frames.into_iter() {
                let (dst_context, dst_device, latency) = (self.mapper)(&n, device_id);
                self.pending_frames.push(PendingFrame::new(
                    self.current_time + latency.unwrap_or(Duration::from_millis(0)),
                    PendingFrameData::<N> { data: frame, dst_context, dst_device },
                ));
            }
        }
    }

    /// Calculates the next `Instant` when events are available.
    ///
    /// Returns the smallest `Instant` greater than or equal to `current_time`
    /// for which an event is available. If no events are available, returns
    /// `None`.
    fn next_step(&self) -> Option<Instant> {
        // get earliest timer in all contexts:
        let next_timer = self
            .contexts
            .iter()
            .filter_map(|(n, ctx)| match ctx.dispatcher.timer_events.peek() {
                Some(tmr) => Some(tmr.0),
                None => None,
            })
            .min();
        /// get the instant for the next packet
        let next_packet_due = self.pending_frames.peek().map(|t| t.0);

        // Return the earliest of them both, and protect against returning a
        // time in the past.
        match next_timer {
            Some(t) if next_packet_due.is_some() => Some(t).min(next_packet_due),
            Some(t) => Some(t),
            None => next_packet_due,
        }
        .map(|t| t.max(self.current_time))
    }

    /// Runs the dummy network simulation until it is starved of events.
    ///
    /// Runs `step` until it returns a `StepResult` where `is_idle` is `true` or
    /// a total of 1,000,000 steps is performed. The imposed limit in steps is
    /// there to prevent the call from blocking; reaching that limit should be
    /// considered a logic error.
    ///
    /// # Panics
    ///
    /// See [`step`] for possible panic conditions.
    pub(crate) fn run_until_idle(&mut self) -> Result<(), LoopLimitReachedError> {
        for _ in 0..1_000_000 {
            if self.step().is_idle() {
                return Ok(());
            }
        }
        debug!("DummyNetwork seems to have gotten stuck in a loop.");
        Err(LoopLimitReachedError)
    }

    /// Runs the dummy network simulation until it is starved of events or
    /// `stop` returns `true`.
    ///
    /// Runs `step` until it returns a `StepResult` where `is_idle` is `true` or
    /// the provided function `stop` returns `true`, or a total of 1,000,000
    /// steps is performed. The imposed limit in steps is there to prevent the
    /// call from blocking; reaching that limit should be considered a logic
    /// error.
    ///
    /// # Panics
    ///
    /// See [`step`] for possible panic conditions.
    pub(crate) fn run_until_idle_or<S: Fn(&mut Self) -> bool>(
        &mut self,
        stop: S,
    ) -> Result<(), LoopLimitReachedError> {
        for _ in 0..1_000_000 {
            if self.step().is_idle() {
                return Ok(());
            } else if stop(self) {
                return Ok(());
            }
        }
        debug!("DummyNetwork seems to have gotten stuck in a loop.");
        Err(LoopLimitReachedError)
    }
}

/// Convenience function to create `DummyNetwork`s
///
/// `new_dummy_network_from_config` creates a `DummyNetwork` with two `Context`s
/// named `a` and `b`. `Context` `a` is created from the configuration provided
/// in `cfg`, and `Context` `b` is created from the symmetric configuration
/// generated by `DummyEventDispatcherConfig::swap`. A default `mapper` function
/// is provided that maps all frames from (`a`, ethernet device `1`) to
/// (`b`, ethernet device `1`) and vice-versa.
pub(crate) fn new_dummy_network_from_config_with_latency<A: IpAddress, N>(
    a: N,
    b: N,
    cfg: DummyEventDispatcherConfig<A>,
    latency: Option<Duration>,
) -> DummyNetwork<N, impl Fn(&N, DeviceId) -> (N, DeviceId, Option<Duration>)>
where
    N: Eq + Hash + Clone + std::fmt::Debug,
{
    let bob = DummyEventDispatcherBuilder::from_config(cfg.swap()).build();
    let alice = DummyEventDispatcherBuilder::from_config(cfg).build();
    let contexts = vec![(a.clone(), alice), (b.clone(), bob)].into_iter();
    DummyNetwork::<N, _>::new(contexts, move |net, device_id| {
        if *net == a {
            (b.clone(), DeviceId::new_ethernet(1), latency)
        } else {
            (a.clone(), DeviceId::new_ethernet(1), latency)
        }
    })
}

/// Convenience function to create `DummyNetwork`s with no latency
///
/// Creates a `DummyNetwork` by calling
/// [`new_dummy_network_from_config_with_latency`] with `latency` set to `None`.
pub(crate) fn new_dummy_network_from_config<A: IpAddress, N>(
    a: N,
    b: N,
    cfg: DummyEventDispatcherConfig<A>,
) -> DummyNetwork<N, impl Fn(&N, DeviceId) -> (N, DeviceId, Option<Duration>)>
where
    N: Eq + Hash + Clone + std::fmt::Debug,
{
    new_dummy_network_from_config_with_latency(a, b, cfg, None)
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::ip::{self, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
    use crate::wire::icmp::{
        IcmpDestUnreachable, IcmpEchoReply, IcmpEchoRequest, IcmpPacketBuilder, IcmpUnusedCode,
        Icmpv4DestUnreachableCode,
    };
    use crate::TimerIdInner;
    use packet::{Buf, BufferSerializer, Serializer};
    use std::time::Duration;

    #[test]
    fn test_parse_ethernet_frame() {
        use crate::wire::testdata::ARP_REQUEST;
        let (body, src_mac, dst_mac, ethertype) = parse_ethernet_frame(ARP_REQUEST).unwrap();
        assert_eq!(body, &ARP_REQUEST[14..]);
        assert_eq!(src_mac, Mac::new([20, 171, 197, 116, 32, 52]));
        assert_eq!(dst_mac, Mac::new([255, 255, 255, 255, 255, 255]));
        assert_eq!(ethertype, Some(EtherType::Arp));
    }

    #[test]
    fn test_parse_ip_packet() {
        use crate::wire::testdata::icmp_redirect::IP_PACKET_BYTES;
        let (body, src_ip, dst_ip, proto) = parse_ip_packet::<Ipv4>(IP_PACKET_BYTES).unwrap();
        assert_eq!(body, &IP_PACKET_BYTES[20..]);
        assert_eq!(src_ip, Ipv4Addr::new([10, 123, 0, 2]));
        assert_eq!(dst_ip, Ipv4Addr::new([10, 123, 0, 1]));
        assert_eq!(proto, IpProto::Icmp);

        use crate::wire::testdata::icmp_echo_v6::REQUEST_IP_PACKET_BYTES;
        let (body, src_ip, dst_ip, proto) =
            parse_ip_packet::<Ipv6>(REQUEST_IP_PACKET_BYTES).unwrap();
        assert_eq!(body, &REQUEST_IP_PACKET_BYTES[40..]);
        assert_eq!(src_ip, Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]));
        assert_eq!(dst_ip, Ipv6Addr::new([0xFE, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]));
        assert_eq!(proto, IpProto::Icmpv6);
    }

    #[test]
    fn test_parse_ip_packet_in_ethernet_frame() {
        use crate::wire::testdata::tls_client_hello::*;
        let (body, src_mac, dst_mac, src_ip, dst_ip, proto) =
            parse_ip_packet_in_ethernet_frame::<Ipv4>(ETHERNET_FRAME_BYTES).unwrap();
        assert_eq!(body, &(ETHERNET_FRAME_BYTES[ETHERNET_BODY_RANGE])[IP_BODY_RANGE]);
        assert_eq!(src_mac, ETHERNET_SRC_MAC);
        assert_eq!(dst_mac, ETHERNET_DST_MAC);
        assert_eq!(src_ip, IP_SRC_IP);
        assert_eq!(dst_ip, IP_DST_IP);
        assert_eq!(proto, IpProto::Tcp);
    }

    #[test]
    fn test_parse_icmp_packet() {
        set_logger_for_test();
        use crate::wire::testdata::icmp_dest_unreachable::*;
        let (body, ..) = parse_ip_packet::<Ipv4>(&IP_PACKET_BYTES).unwrap();
        let (_, code) = parse_icmp_packet::<Ipv4, _, IcmpDestUnreachable, _>(
            body,
            Ipv4Addr::new([172, 217, 6, 46]),
            Ipv4Addr::new([192, 168, 0, 105]),
            |_| {},
        )
        .unwrap();
        assert_eq!(code, Icmpv4DestUnreachableCode::DestHostUnreachable);
    }

    #[test]
    fn test_parse_icmp_packet_in_ip_packet_in_ethernet_frame() {
        set_logger_for_test();
        use crate::wire::testdata::icmp_echo_ethernet::*;
        let (src_mac, dst_mac, src_ip, dst_ip, _, _) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, IcmpEchoReply, _>(
                &REPLY_ETHERNET_FRAME_BYTES,
                |_| {},
            )
            .unwrap();
        assert_eq!(src_mac, Mac::new([0x50, 0xc7, 0xbf, 0x1d, 0xf4, 0xd2]));
        assert_eq!(dst_mac, Mac::new([0x8c, 0x85, 0x90, 0xc9, 0xc9, 0x00]));
        assert_eq!(src_ip, Ipv4Addr::new([172, 217, 6, 46]));
        assert_eq!(dst_ip, Ipv4Addr::new([192, 168, 0, 105]));
    }

    #[test]
    fn test_dummy_network_transmits_packets() {
        set_logger_for_test();
        let mut net = new_dummy_network_from_config("alice", "bob", DUMMY_CONFIG_V4);

        // alice sends bob a ping:
        ip::send_ip_packet(
            net.context("alice"),
            DUMMY_CONFIG_V4.remote_ip,
            ip::IpProto::Icmp,
            |_| {
                let req = IcmpEchoRequest::new(0, 0);
                let req_body = &[1, 2, 3, 4];
                BufferSerializer::new_vec(Buf::new(req_body.to_vec(), ..)).encapsulate(
                    IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                        DUMMY_CONFIG_V4.local_ip,
                        DUMMY_CONFIG_V4.remote_ip,
                        IcmpUnusedCode,
                        req,
                    ),
                )
            },
        );

        // send from alice to bob
        assert_eq!(net.step().frames_sent(), 1);
        // respond from bob to alice
        assert_eq!(net.step().frames_sent(), 1);
        // should've starved all events:
        assert!(net.step().is_idle());
    }

    #[test]
    fn test_dummy_network_timers() {
        set_logger_for_test();
        let mut net = new_dummy_network_from_config(1, 2, DUMMY_CONFIG_V4);

        net.context(1)
            .dispatcher
            .schedule_timeout(Duration::from_secs(1), TimerId(TimerIdInner::Nop(1)));
        net.context(2)
            .dispatcher
            .schedule_timeout(Duration::from_secs(2), TimerId(TimerIdInner::Nop(2)));
        net.context(2)
            .dispatcher
            .schedule_timeout(Duration::from_secs(3), TimerId(TimerIdInner::Nop(3)));
        net.context(1)
            .dispatcher
            .schedule_timeout(Duration::from_secs(4), TimerId(TimerIdInner::Nop(4)));

        net.context(1)
            .dispatcher
            .schedule_timeout(Duration::from_secs(5), TimerId(TimerIdInner::Nop(5)));
        net.context(2)
            .dispatcher
            .schedule_timeout(Duration::from_secs(5), TimerId(TimerIdInner::Nop(6)));

        // no timers fired before:
        assert_eq!(*net.context(1).state.test_counters.get("timer::nop"), 0);
        assert_eq!(*net.context(2).state.test_counters.get("timer::nop"), 0);
        assert_eq!(net.step().timers_fired(), 1);
        // only timer in context 1 should have fired:
        assert_eq!(*net.context(1).state.test_counters.get("timer::nop"), 1);
        assert_eq!(*net.context(2).state.test_counters.get("timer::nop"), 0);
        assert_eq!(net.step().timers_fired(), 1);
        // only timer in context 2 should have fired:
        assert_eq!(*net.context(1).state.test_counters.get("timer::nop"), 1);
        assert_eq!(*net.context(2).state.test_counters.get("timer::nop"), 1);
        assert_eq!(net.step().timers_fired(), 1);
        // only timer in context 2 should have fired:
        assert_eq!(*net.context(1).state.test_counters.get("timer::nop"), 1);
        assert_eq!(*net.context(2).state.test_counters.get("timer::nop"), 2);
        assert_eq!(net.step().timers_fired(), 1);
        // only timer in context 1 should have fired:
        assert_eq!(*net.context(1).state.test_counters.get("timer::nop"), 2);
        assert_eq!(*net.context(2).state.test_counters.get("timer::nop"), 2);
        assert_eq!(net.step().timers_fired(), 2);
        // both timers have fired at the same time:
        assert_eq!(*net.context(1).state.test_counters.get("timer::nop"), 3);
        assert_eq!(*net.context(2).state.test_counters.get("timer::nop"), 3);

        assert!(net.step().is_idle());
        // check that current time on contexts tick together:
        let t1 = net.context(1).dispatcher.current_time;
        let t2 = net.context(2).dispatcher.current_time;
        assert_eq!(t1, t2);
    }

    #[test]
    fn test_dummy_network_until_idle() {
        set_logger_for_test();
        let mut net = new_dummy_network_from_config(1, 2, DUMMY_CONFIG_V4);
        net.context(1)
            .dispatcher
            .schedule_timeout(Duration::from_secs(1), TimerId(TimerIdInner::Nop(1)));
        net.context(2)
            .dispatcher
            .schedule_timeout(Duration::from_secs(2), TimerId(TimerIdInner::Nop(2)));
        net.context(2)
            .dispatcher
            .schedule_timeout(Duration::from_secs(3), TimerId(TimerIdInner::Nop(3)));

        net.run_until_idle_or(|net| {
            *net.context(1).state.test_counters.get("timer::nop") == 1
                && *net.context(2).state.test_counters.get("timer::nop") == 1
        })
        .unwrap();
        // assert that we stopped before all times were fired, meaning we can
        // step again:
        assert_eq!(net.step().timers_fired(), 1);
    }

    #[test]
    fn test_instant_and_data() {
        // verify implementation of InstantAndData to be used as a complex type
        // in a BinaryHeap:
        let mut heap = BinaryHeap::<InstantAndData<usize>>::new();
        let now = Instant::now();

        fn new_data(time: Instant, id: usize) -> InstantAndData<usize> {
            InstantAndData::new(time, id)
        }

        heap.push(new_data(now + Duration::from_secs(1), 1));
        heap.push(new_data(now + Duration::from_secs(2), 2));

        // earlier timer is popped first
        assert!(heap.pop().unwrap().1 == 1);
        assert!(heap.pop().unwrap().1 == 2);
        assert!(heap.pop().is_none());

        heap.push(new_data(now + Duration::from_secs(1), 1));
        heap.push(new_data(now + Duration::from_secs(1), 1));

        // can pop twice with identical data:
        assert!(heap.pop().unwrap().1 == 1);
        assert!(heap.pop().unwrap().1 == 1);
        assert!(heap.pop().is_none());
    }

    #[test]
    fn test_delayed_packets() {
        set_logger_for_test();
        // create a network that takes 5ms to get any packet to go through:
        let mut net = new_dummy_network_from_config_with_latency(
            "alice",
            "bob",
            DUMMY_CONFIG_V4,
            Some(Duration::from_millis(5)),
        );

        // alice sends bob a ping:
        ip::send_ip_packet(
            net.context("alice"),
            DUMMY_CONFIG_V4.remote_ip,
            ip::IpProto::Icmp,
            |_| {
                let req = IcmpEchoRequest::new(0, 0);
                let req_body = &[1, 2, 3, 4];
                BufferSerializer::new_vec(Buf::new(req_body.to_vec(), ..)).encapsulate(
                    IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                        DUMMY_CONFIG_V4.local_ip,
                        DUMMY_CONFIG_V4.remote_ip,
                        IcmpUnusedCode,
                        req,
                    ),
                )
            },
        );

        net.context("alice")
            .dispatcher
            .schedule_timeout(Duration::from_millis(3), TimerId(TimerIdInner::Nop(1)));
        net.context("bob")
            .dispatcher
            .schedule_timeout(Duration::from_millis(7), TimerId(TimerIdInner::Nop(2)));
        net.context("bob")
            .dispatcher
            .schedule_timeout(Duration::from_millis(10), TimerId(TimerIdInner::Nop(1)));

        // order of expected events is as follows:
        // - Alice's timer expires at t = 3
        // - Bob receives Alice's packet at t = 5
        // - Bob's timer expires at t = 7
        // - Alice receives Bob's response and Bob's last timer fires at t = 10

        fn assert_full_state<F>(
            net: &mut DummyNetwork<&'static str, F>,
            alice_nop: usize,
            bob_nop: usize,
            bob_echo_request: usize,
            alice_echo_response: usize,
        ) where
            F: Fn(&&'static str, DeviceId) -> (&'static str, DeviceId, Option<Duration>),
        {
            let alice = net.context("alice");
            assert_eq!(*alice.state.test_counters.get("timer::nop"), alice_nop);
            assert_eq!(
                *alice.state.test_counters.get("receive_icmp_packet::echo_reply"),
                alice_echo_response
            );

            let bob = net.context("bob");
            assert_eq!(*bob.state.test_counters.get("timer::nop"), bob_nop);
            assert_eq!(
                *bob.state.test_counters.get("receive_icmp_packet::echo_request"),
                bob_echo_request
            );
        }

        assert_eq!(net.step().timers_fired(), 1);
        assert_full_state(&mut net, 1, 0, 0, 0);
        assert_eq!(net.step().frames_sent(), 1);
        assert_full_state(&mut net, 1, 0, 1, 0);
        assert_eq!(net.step().timers_fired(), 1);
        assert_full_state(&mut net, 1, 1, 1, 0);
        let step = net.step();
        assert_eq!(step.frames_sent(), 1);
        assert_eq!(step.timers_fired(), 1);
        assert_full_state(&mut net, 1, 2, 1, 1);

        // should've starved all events:
        assert!(net.step().is_idle());
    }
}
