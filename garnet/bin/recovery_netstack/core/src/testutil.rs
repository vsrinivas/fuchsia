// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Testing-related utilities.

#![allow(deprecated)]

use std::collections::{BTreeMap, HashMap};
use std::sync::Once;
use std::time::{Duration, Instant};

use byteorder::{ByteOrder, NativeEndian};
use packet::ParseBuffer;
use rand::{SeedableRng, XorShiftRng};

use crate::device::ethernet::{EtherType, Mac};
use crate::device::{DeviceId, DeviceLayerEventDispatcher};
use crate::error::ParseError;
use crate::ip::{Ip, IpExt, IpPacket, IpProto};
use crate::transport::udp::UdpEventDispatcher;
use crate::transport::TransportLayerEventDispatcher;
use crate::wire::ethernet::EthernetFrame;
use crate::{handle_timeout, Context, EventDispatcher, TimerId};

/// Create a new deterministic RNG from a seed.
pub fn new_rng(mut seed: u64) -> XorShiftRng {
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
pub struct TestCounters {
    data: HashMap<String, usize>,
}

impl TestCounters {
    pub fn increment(&mut self, key: &str) {
        *(self.data.entry(key.to_string()).or_insert(0)) += 1;
    }

    pub fn get(&self, key: &str) -> &usize {
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
/// Call this method at the beginning of the test for which logging is desired.  This function sets
/// global program state, so all tests that run after this function is called will use the logger.
pub fn set_logger_for_test() {
    // log::set_logger will panic if called multiple times; using a Once makes
    // set_logger_for_test idempotent
    LOGGER_ONCE.call_once(|| {
        log::set_logger(&LOGGER).unwrap();
        log::set_max_level(log::LevelFilter::Trace);
    })
}

/// Skip current (fake) time forward to trigger the next timer event.
///
/// Returns true if a timer was triggered, false if there were no timers waiting to be
/// triggered.
pub fn trigger_next_timer(ctx: &mut Context<DummyEventDispatcher>) -> bool {
    match ctx
        .dispatcher
        .timer_events
        .keys()
        .next()
        .map(|t| *t)
        .and_then(|t| ctx.dispatcher.timer_events.remove(&t).map(|id| (t, id)))
    {
        Some((t, id)) => {
            ctx.dispatcher.current_time = t;
            handle_timeout(ctx, id);
            true
        }
        None => false,
    }
}

/// Parse an ethernet frame.
///
/// `parse_ethernet_frame` parses an ethernet frame, returning the body along
/// with some important header fields.
pub fn parse_ethernet_frame(
    mut buf: &[u8],
) -> Result<(&[u8], Mac, Mac, Option<EtherType>), ParseError> {
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
pub fn parse_ip_packet<I: Ip>(
    mut buf: &[u8],
) -> Result<(&[u8], I::Addr, I::Addr, IpProto), ParseError> {
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

/// Parse an IP packet in an Ethernet frame.
///
/// `parse_ip_packet_in_ethernet_frame` parses an IP packet in an Ethernet
/// frame, returning the body of the IP packet along with some important fields
/// from both the IP and Ethernet headers.
pub fn parse_ip_packet_in_ethernet_frame<I: Ip>(
    buf: &[u8],
) -> Result<(&[u8], Mac, Mac, Option<EtherType>, I::Addr, I::Addr, IpProto), ParseError> {
    let (body, src_mac, dst_mac, ethertype) = parse_ethernet_frame(buf)?;
    let (body, src_ip, dst_ip, proto) = parse_ip_packet::<I>(body)?;
    Ok((body, src_mac, dst_mac, ethertype, src_ip, dst_ip, proto))
}

/// A dummy `EventDispatcher` used for testing.
///
/// A `DummyEventDispatcher` implements the `EventDispatcher` interface for
/// testing purposes. It provides facilities to inspect the history of what
/// events have been emitted to the system.
pub struct DummyEventDispatcher {
    frames_sent: Vec<(DeviceId, Vec<u8>)>,
    timer_events: BTreeMap<Instant, TimerId>,
    current_time: Instant,
}

impl DummyEventDispatcher {
    pub fn frames_sent(&self) -> &[(DeviceId, Vec<u8>)] {
        &self.frames_sent
    }

    /// Get an ordered list of all scheduled timer events
    pub fn timer_events<'a>(&'a self) -> impl Iterator<Item = (&'a Instant, &'a TimerId)> {
        self.timer_events.iter()
    }

    /// Get the current (fake) time
    pub fn current_time(self) -> Instant {
        self.current_time
    }
}

impl Default for DummyEventDispatcher {
    fn default() -> DummyEventDispatcher {
        DummyEventDispatcher {
            frames_sent: vec![],
            timer_events: BTreeMap::new(),
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
        self.timer_events.insert(time, id);
        ret
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<Instant> {
        // There is the invariant that there can only be one timer event per TimerId, so we only
        // need to remove at most one element from timer_events.

        match self.timer_events.iter().find_map(|(instant, event_timer_id)| {
            if *event_timer_id == id {
                Some(*instant)
            } else {
                None
            }
        }) {
            Some(instant) => {
                self.timer_events.remove(&instant);
                Some(instant)
            }
            None => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};

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
        let (body, src_mac, dst_mac, ethertype, src_ip, dst_ip, proto) =
            parse_ip_packet_in_ethernet_frame::<Ipv4>(ETHERNET_FRAME_BYTES).unwrap();
        assert_eq!(body, &(ETHERNET_FRAME_BYTES[ETHERNET_BODY_RANGE])[IP_BODY_RANGE]);
        assert_eq!(src_mac, ETHERNET_SRC_MAC);
        assert_eq!(dst_mac, ETHERNET_DST_MAC);
        assert_eq!(ethertype, Some(EtherType::Ipv4));
        assert_eq!(src_ip, IP_SRC_IP);
        assert_eq!(dst_ip, IP_DST_IP);
        assert_eq!(proto, IpProto::Tcp);
    }
}
