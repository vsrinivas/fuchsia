// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Ethernet protocol.

use std::collections::{HashMap, HashSet, VecDeque};

use log::debug;
use net_types::ethernet::Mac;
use net_types::ip::{AddrSubnet, Ip, IpAddr, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use net_types::{BroadcastAddress, MulticastAddr, MulticastAddress, UnicastAddress};
use packet::{Buf, BufferMut, Nested, Serializer};
use specialize_ip_macro::specialize_ip_address;

use crate::device::arp::{self, ArpDevice, ArpHardwareType, ArpState};
use crate::device::{ndp, ndp::NdpState};
use crate::device::{DeviceId, FrameDestination};
use crate::wire::arp::peek_arp_types;
use crate::wire::ethernet::{EthernetFrame, EthernetFrameBuilder};
use crate::{Context, EventDispatcher, StackState};

const ETHERNET_MAX_PENDING_FRAMES: usize = 10;

impl From<Mac> for FrameDestination {
    fn from(mac: Mac) -> FrameDestination {
        if mac.is_broadcast() {
            FrameDestination::Broadcast
        } else if mac.is_multicast() {
            FrameDestination::Multicast
        } else {
            debug_assert!(mac.is_unicast());
            FrameDestination::Unicast
        }
    }
}

impl ndp::LinkLayerAddress for Mac {
    const BYTES_LENGTH: usize = 6;

    fn bytes(&self) -> &[u8] {
        self.as_ref()
    }

    fn from_bytes(bytes: &[u8]) -> Self {
        // assert that contract is being held:
        debug_assert!(bytes.len() == Self::BYTES_LENGTH);
        let mut b = [0; Self::BYTES_LENGTH];
        b.copy_from_slice(bytes);
        Self::new(b)
    }
}

create_protocol_enum!(
    /// An EtherType number.
    #[derive(Copy, Clone, Hash, Eq, PartialEq)]
    pub(crate) enum EtherType: u16 {
        Ipv4, 0x0800, "IPv4";
        Arp, 0x0806, "ARP";
        Ipv6, 0x86DD, "IPv6";
        _, "EtherType {}";
    }
);

/// The state associated with an Ethernet device.
pub(crate) struct EthernetDeviceState {
    mac: Mac,
    mtu: u32,
    ipv4_addr_sub: Option<AddrSubnet<Ipv4Addr>>,
    ipv6_addr_sub: Option<AddrSubnet<Ipv6Addr>>,
    ipv4_multicast_groups: HashSet<MulticastAddr<Ipv4Addr>>,
    ipv6_multicast_groups: HashSet<MulticastAddr<Ipv6Addr>>,
    ipv4_arp: ArpState<Ipv4Addr, EthernetArpDevice>,
    ndp: ndp::NdpState<EthernetNdpDevice>,
    // pending_frames stores a list of serialized frames indexed by their
    // destination IP addresses. The frames contain an entire EthernetFrame
    // body and the MTU check is performed before queueing them here.
    pending_frames: HashMap<IpAddr, VecDeque<Vec<u8>>>,
}

impl EthernetDeviceState {
    /// Construct a new `EthernetDeviceState`.
    ///
    /// `new` constructs a new `EthernetDeviceState` with the given MAC address
    /// and MTU. The MTU will be taken as a limit on the size of Ethernet
    /// payloads - the Ethernet header is not counted towards the MTU.
    pub(crate) fn new(mac: Mac, mtu: u32) -> EthernetDeviceState {
        // TODO(joshlf): Add a minimum MTU for all Ethernet devices such that
        //  you cannot create an `EthernetDeviceState` with an MTU smaller than
        //  the minimum. The absolute minimum needs to be at least the minimum
        //  body size of an Ethernet frame. For IPv6-capable devices, the
        //  minimum needs to be higher - the IPv6 minimum MTU. The easy path is
        //  to simply use the IPv6 minimum MTU as the minimum in all cases,
        //  although we may at some point want to figure out how to configure
        //  devices which don't support IPv6, and allow smaller MTUs for those
        //  devices.
        //  A few questions:
        //  - How do we wire error information back up the call stack? Should
        //  this just return a Result or something?

        let mut ipv6_multicast_groups = HashSet::new();

        // We know the call to `unwrap` will not panic because
        // `to_solicited_node_address` returns a multicast address,
        // so `new` will not return `None`.
        ipv6_multicast_groups.insert(
            MulticastAddr::new(mac.to_ipv6_link_local().to_solicited_node_address()).unwrap(),
        );

        // TODO(ghanan): Perform NDP's DAD on the link local address BEFORE receiving
        //               packets destined to it.

        EthernetDeviceState {
            mac,
            mtu,
            ipv4_addr_sub: None,
            ipv6_addr_sub: None,
            ipv4_multicast_groups: HashSet::new(),
            ipv6_multicast_groups,
            ipv4_arp: ArpState::default(),
            ndp: NdpState::default(),
            pending_frames: HashMap::new(),
        }
    }

    /// Adds a pending frame `frame` associated with `local_addr` to the list
    /// of pending frames in the current device state.
    ///
    /// If an older frame had to be dropped because it exceeds the maximum
    /// allowed number of pending frames, it is returned.
    fn add_pending_frame(&mut self, local_addr: IpAddr, frame: Vec<u8>) -> Option<Vec<u8>> {
        let buff = self.pending_frames.entry(local_addr).or_insert_with(Default::default);
        buff.push_back(frame);
        if buff.len() > ETHERNET_MAX_PENDING_FRAMES {
            buff.pop_front()
        } else {
            None
        }
    }

    /// Takes all pending frames associated with address `local_addr`.
    fn take_pending_frames(&mut self, local_addr: IpAddr) -> Option<impl Iterator<Item = Vec<u8>>> {
        match self.pending_frames.remove(&local_addr) {
            Some(mut buff) => Some(buff.into_iter()),
            None => None,
        }
    }
}

/// An extension trait adding IP-related functionality to `Ipv4` and `Ipv6`.
pub(crate) trait EthernetIpExt: Ip {
    const ETHER_TYPE: EtherType;
}

impl<I: Ip> EthernetIpExt for I {
    default const ETHER_TYPE: EtherType = EtherType::Ipv4;
}

impl EthernetIpExt for Ipv4 {
    const ETHER_TYPE: EtherType = EtherType::Ipv4;
}

impl EthernetIpExt for Ipv6 {
    const ETHER_TYPE: EtherType = EtherType::Ipv6;
}

/// Send an IP packet in an Ethernet frame.
///
/// `send_ip_frame` accepts a device ID, a local IP address, and a
/// `SerializationRequest`. It computes the routing information and serializes
/// the request in a new Ethernet frame and sends it.
#[specialize_ip_address]
pub(crate) fn send_ip_frame<D: EventDispatcher, A: IpAddress, S: Serializer>(
    ctx: &mut Context<D>,
    device_id: usize,
    local_addr: A,
    body: S,
) -> Result<(), S> {
    let state = get_device_state_mut(ctx.state_mut(), device_id);
    let (local_mac, mtu) = (state.mac, state.mtu);

    let dst_mac = match MulticastAddr::new(local_addr) {
        Some(multicast) => Ok(Mac::from(&multicast)),
        None => {
            #[ipv4addr]
            {
                arp::lookup::<_, _, EthernetArpDevice>(ctx, device_id, local_mac, local_addr)
                    .ok_or(IpAddr::V4(local_addr))
            }
            #[ipv6addr]
            {
                ndp::lookup::<_, EthernetNdpDevice>(ctx, device_id, local_addr)
                    .ok_or(IpAddr::V6(local_addr))
            }
        }
    };

    match dst_mac {
        Ok(dst_mac) => ctx
            .dispatcher_mut()
            .send_frame(
                DeviceId::new_ethernet(device_id),
                body.with_mtu(mtu as usize).encapsulate(EthernetFrameBuilder::new(
                    local_mac,
                    dst_mac,
                    A::Version::ETHER_TYPE,
                )),
            )
            .map_err(|ser| ser.into_inner().into_inner()),
        Err(local_addr) => {
            let state = get_device_state_mut(ctx.state_mut(), device_id);
            let dropped = state.add_pending_frame(
                local_addr,
                body.with_mtu(mtu as usize)
                    .serialize_vec_outer()
                    .map_err(|ser| ser.1.into_inner())?
                    .as_ref()
                    .to_vec(),
            );
            if let Some(dropped) = dropped {
                // TODO(brunodalbo): Is it ok to silently just let this drop? Or
                //  should the IP layer be notified in any way?
                log_unimplemented!((), "Ethernet dropped frame because ran out of allowable space");
            }
            Ok(())
        }
    }
}

/// Receive an Ethernet frame from the network.
pub(crate) fn receive_frame<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    device_id: usize,
    mut buffer: B,
) {
    let frame = if let Ok(frame) = buffer.parse::<EthernetFrame<_>>() {
        frame
    } else {
        // TODO(joshlf): Do something else?
        return;
    };

    let (src, dst) = (frame.src_mac(), frame.dst_mac());
    let device = DeviceId::new_ethernet(device_id);
    if dst != get_device_state(ctx.state(), device_id).mac && !dst.is_broadcast() {
        // TODO(joshlf): What about multicast MACs?
        return;
    }
    let frame_dst = FrameDestination::from(dst);

    match frame.ethertype() {
        Some(EtherType::Arp) => {
            let types = if let Ok(types) = peek_arp_types(buffer.as_ref()) {
                types
            } else {
                // TODO(joshlf): Do something else here?
                return;
            };
            match types {
                (ArpHardwareType::Ethernet, EtherType::Ipv4) => {
                    crate::device::arp::receive_arp_packet::<D, Ipv4Addr, EthernetArpDevice, _>(
                        ctx, device_id, src, dst, buffer,
                    )
                }
                types => debug!("got ARP packet for unsupported types: {:?}", types),
            }
        }
        Some(EtherType::Ipv4) => {
            crate::ip::receive_ip_packet::<_, _, Ipv4>(ctx, device, frame_dst, buffer)
        }
        Some(EtherType::Ipv6) => {
            crate::ip::receive_ip_packet::<_, _, Ipv6>(ctx, device, frame_dst, buffer)
        }
        Some(EtherType::Other(_)) | None => {} // TODO(joshlf)
    }
}

/// Get the IP address and subnet associated with this device.
#[specialize_ip_address]
pub(crate) fn get_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: usize,
) -> Option<AddrSubnet<A>> {
    #[ipv4addr]
    return get_device_state(ctx.state(), device_id).ipv4_addr_sub;
    #[ipv6addr]
    return get_device_state(ctx.state(), device_id).ipv6_addr_sub;
}

/// Get the IPv6 link-local address associated with this device.
///
/// The IPv6 link-local address returned is constructed from this device's MAC
/// address.
pub(crate) fn get_ipv6_link_local_addr<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: usize,
) -> Ipv6Addr {
    // TODO(brunodalbo) the link local address is subject to the same collision
    //  verifications as prefix global addresses, we should keep a state machine
    //  about that check and cache the adopted address. For now, we just compose
    //  the link-local from the ethernet MAC.
    get_device_state(ctx.state(), device_id).mac.to_ipv6_link_local()
}

/// Set the IP address and subnet associated with this device.
#[specialize_ip_address]
pub(crate) fn set_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: usize,
    addr_sub: AddrSubnet<A>,
) {
    #[ipv4addr]
    get_device_state_mut(ctx.state_mut(), device_id).ipv4_addr_sub = Some(addr_sub);
    #[ipv6addr]
    get_device_state_mut(ctx.state_mut(), device_id).ipv6_addr_sub = Some(addr_sub);
}

/// Add `device` to a multicast group `multicast_addr`.
///
/// If `device` is already in the multicast group `multicast_addr`,
/// `join_ip_multicast` does nothing.
#[specialize_ip_address]
pub(crate) fn join_ip_multicast<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: usize,
    multicast_addr: MulticastAddr<A>,
) {
    #[ipv4addr]
    get_device_state_mut(ctx.state_mut(), device_id).ipv4_multicast_groups.insert(multicast_addr);

    #[ipv6addr]
    get_device_state_mut(ctx.state_mut(), device_id).ipv6_multicast_groups.insert(multicast_addr);
}

/// Remove `device` from a multicast group `multicast_addr`.
///
/// If `device` is not in the multicast group `multicast_addr`,
/// `leave_ip_multicast` does nothing.
#[specialize_ip_address]
pub(crate) fn leave_ip_multicast<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: usize,
    multicast_addr: MulticastAddr<A>,
) {
    #[ipv4addr]
    get_device_state_mut(ctx.state_mut(), device_id).ipv4_multicast_groups.remove(&multicast_addr);

    #[ipv6addr]
    get_device_state_mut(ctx.state_mut(), device_id).ipv6_multicast_groups.remove(&multicast_addr);
}

/// Is `device` in the IP multicast group `multicast_addr`?
#[specialize_ip_address]
pub(crate) fn is_in_ip_multicast<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
    device_id: usize,
    multicast_addr: MulticastAddr<A>,
) -> bool {
    #[ipv4addr]
    return get_device_state(ctx.state(), device_id)
        .ipv4_multicast_groups
        .contains(&multicast_addr);

    #[ipv6addr]
    return get_device_state(ctx.state(), device_id)
        .ipv6_multicast_groups
        .contains(&multicast_addr);
}

/// Get the MTU associated with this device.
pub(crate) fn get_mtu<D: EventDispatcher>(ctx: &mut Context<D>, device_id: usize) -> u32 {
    get_device_state(ctx.state(), device_id).mtu
}

/// Insert a static entry into this device's ARP table.
///
/// This will cause any conflicting dynamic entry to be removed, and
/// any future conflicting gratuitous ARPs to be ignored.
pub(crate) fn insert_static_arp_table_entry<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: usize,
    addr: Ipv4Addr,
    mac: Mac,
) {
    crate::device::arp::insert_static::<D, Ipv4Addr, EthernetArpDevice>(ctx, device_id, addr, mac);
}

/// Insert an entry into this device's NDP table.
pub(crate) fn insert_ndp_table_entry<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: usize,
    addr: Ipv6Addr,
    mac: Mac,
) {
    ndp::insert_neighbor::<D, EthernetNdpDevice>(ctx, device_id, addr, mac)
}

fn get_device_state_mut<D: EventDispatcher>(
    state: &mut StackState<D>,
    device_id: usize,
) -> &mut EthernetDeviceState {
    // TODO(joshlf): Sometimes we want lookups to be infallible (if we know that
    // the device exists), but sometimes we want to report an error to the user.
    // Right now, this is a DoS vector.
    state
        .device
        .ethernet
        .get_mut(device_id)
        .unwrap_or_else(|| panic!("no such Ethernet device: {}", device_id))
}

fn get_device_state<D: EventDispatcher>(
    state: &StackState<D>,
    device_id: usize,
) -> &EthernetDeviceState {
    // TODO(joshlf): Sometimes we want lookups to be infallible (if we know that
    // the device exists), but sometimes we want to report an error to the user.
    // Right now, this is a DoS vector.
    state
        .device
        .ethernet
        .get(device_id)
        .unwrap_or_else(|| panic!("no such Ethernet device: {}", device_id))
}

/// Dummy type used to implement ArpDevice.
pub(super) struct EthernetArpDevice;

impl ArpDevice<Ipv4Addr> for EthernetArpDevice {
    type HardwareAddr = Mac;
    const BROADCAST: Mac = Mac::BROADCAST;

    fn send_arp_frame<D: EventDispatcher, S: Serializer>(
        ctx: &mut Context<D>,
        device_id: usize,
        dst: Self::HardwareAddr,
        body: S,
    ) -> Result<(), S> {
        let src = get_device_state(ctx.state_mut(), device_id).mac;
        ctx.dispatcher_mut()
            .send_frame(
                DeviceId::new_ethernet(device_id),
                body.encapsulate(EthernetFrameBuilder::new(src, dst, EtherType::Arp)),
            )
            .map_err(Nested::into_inner)
    }

    fn get_arp_state<D: EventDispatcher>(
        state: &mut StackState<D>,
        device_id: usize,
    ) -> &mut ArpState<Ipv4Addr, Self> {
        &mut get_device_state_mut(state, device_id).ipv4_arp
    }

    fn get_protocol_addr<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
    ) -> Option<Ipv4Addr> {
        get_device_state(state, device_id).ipv4_addr_sub.map(AddrSubnet::into_addr)
    }

    fn get_hardware_addr<D: EventDispatcher>(state: &StackState<D>, device_id: usize) -> Mac {
        get_device_state(state, device_id).mac
    }

    fn address_resolved<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: usize,
        proto_addr: Ipv4Addr,
        hw_addr: Mac,
    ) {
        mac_resolved(ctx, device_id, IpAddr::V4(proto_addr), hw_addr);
    }

    fn address_resolution_failed<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: usize,
        proto_addr: Ipv4Addr,
    ) {
        mac_resolution_failed(ctx, device_id, IpAddr::V4(proto_addr));
    }
}

/// Dummy type used to implement NdpDevice
pub(crate) struct EthernetNdpDevice;

impl ndp::NdpDevice for EthernetNdpDevice {
    type LinkAddress = Mac;
    const BROADCAST: Mac = Mac::BROADCAST;

    fn get_ndp_state<D: EventDispatcher>(
        state: &mut StackState<D>,
        device_id: usize,
    ) -> &mut ndp::NdpState<Self> {
        &mut get_device_state_mut(state, device_id).ndp
    }

    fn get_link_layer_addr<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
    ) -> Self::LinkAddress {
        get_device_state(state, device_id).mac
    }

    fn get_ipv6_addr<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
    ) -> Option<Ipv6Addr> {
        let state = get_device_state(state, device_id);
        // TODO(brunodalbo) just returning either the configured or EUI
        //  link_local address for now, we need a better structure to keep
        //  a list of IPv6 addresses.
        match state.ipv6_addr_sub {
            Some(addr_sub) => Some(addr_sub.into_addr()),
            None => Some(state.mac.to_ipv6_link_local()),
        }
    }

    fn has_ipv6_addr<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
        address: &Ipv6Addr,
    ) -> bool {
        let state = get_device_state(state, device_id);
        state.ipv6_addr_sub.map_or(false, |addr_sub| addr_sub.into_addr() == *address)
            || state.mac.to_ipv6_link_local() == *address
    }

    fn send_ipv6_frame_to<D: EventDispatcher, S: Serializer>(
        ctx: &mut Context<D>,
        device_id: usize,
        dst: Mac,
        body: S,
    ) -> Result<(), S> {
        let src = get_device_state(ctx.state(), device_id).mac;
        ctx.dispatcher_mut()
            .send_frame(
                DeviceId::new_ethernet(device_id),
                body.encapsulate(EthernetFrameBuilder::new(src, dst, EtherType::Ipv6)),
            )
            .map_err(Nested::into_inner)
    }

    fn send_ipv6_frame<D: EventDispatcher, S: Serializer>(
        ctx: &mut Context<D>,
        device_id: usize,
        next_hop: Ipv6Addr,
        body: S,
    ) -> Result<(), S> {
        send_ip_frame(ctx, device_id, next_hop, body)
    }

    fn get_device_id(id: usize) -> DeviceId {
        DeviceId::new_ethernet(id)
    }

    fn address_resolved<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: usize,
        address: &Ipv6Addr,
        link_address: Self::LinkAddress,
    ) {
        mac_resolved(ctx, device_id, IpAddr::V6(*address), link_address);
    }

    fn address_resolution_failed<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: usize,
        address: &Ipv6Addr,
    ) {
        mac_resolution_failed(ctx, device_id, IpAddr::V6(*address));
    }
}

/// Sends out any pending frames that are waiting for link layer address
/// resolution.
///
/// `mac_resolved` is the common logic used when a link layer address is
/// resolved either by ARP or NDP.
fn mac_resolved<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: usize,
    address: IpAddr,
    dst_mac: Mac,
) {
    let state = get_device_state_mut(ctx.state_mut(), device_id);
    let device_id = DeviceId::new_ethernet(device_id);
    let src_mac = state.mac;
    let ether_type = match &address {
        IpAddr::V4(_) => EtherType::Ipv4,
        IpAddr::V6(_) => EtherType::Ipv6,
    };
    if let Some(pending) = state.take_pending_frames(address) {
        for frame in pending {
            // NOTE(brunodalbo): We already performed MTU checking when we
            //  saved the buffer waiting for address resolution. It should
            //  be noted that the MTU check back then didn't account for
            //  ethernet frame padding required by EthernetFrameBuilder,
            //  but that's fine (as it stands right now) because the MTU
            //  is guaranteed to be larger than an Ethernet minimum frame
            //  body size.
            let res = ctx.dispatcher_mut().send_frame(
                device_id,
                Buf::new(frame, ..)
                    .encapsulate(EthernetFrameBuilder::new(src_mac, dst_mac, ether_type)),
            );
            if let Err(_) = res {
                // TODO(joshlf): Do we want to handle this differently?
                debug!("Failed to send pending frame; MTU changed since frame was queued");
            }
        }
    }
}

/// Clears out any pending frames that are waiting for link layer address
/// resolution.
///
/// `mac_resolution_failed` is the common logic used when a link layer address
/// fails to resolve either by ARP or NDP.
fn mac_resolution_failed<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: usize,
    address: IpAddr,
) {
    // TODO(brunodalbo) what do we do here in regards to the pending frames?
    //  NDP's RFC explicitly states unreachable ICMP messages must be generated:
    //  "If no Neighbor Advertisement is received after MAX_MULTICAST_SOLICIT
    //  solicitations, address resolution has failed. The sender MUST return
    //  ICMP destination unreachable indications with code 3
    //  (Address Unreachable) for each packet queued awaiting address
    //  resolution."
    //  For ARP, we don't have such a clear statement on the RFC, it would make
    //  sense to do the same thing though.
    let state = get_device_state_mut(ctx.state_mut(), device_id);
    if let Some(pending) = state.take_pending_frames(address) {
        log_unimplemented!((), "ethernet mac resolution failed not implemented");
    }
}

#[cfg(test)]
mod tests {
    use packet::Buf;

    use super::*;
    use crate::testutil::{DummyEventDispatcher, DummyEventDispatcherBuilder, DUMMY_CONFIG_V4};

    #[test]
    fn test_mtu() {
        // Test that we send an Ethernet frame whose size is less than the MTU,
        // and that we don't send an Ethernet frame whose size is greater than
        // the MTU.
        fn test(size: usize, expect_frames_sent: usize) {
            let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V4)
                .build::<DummyEventDispatcher>();
            send_ip_frame(&mut ctx, 0, DUMMY_CONFIG_V4.remote_ip, Buf::new(&mut vec![0; size], ..));
            assert_eq!(ctx.dispatcher().frames_sent().len(), expect_frames_sent);
        }

        // The Ethernet device MTU currently defaults to IPV6_MIN_MTU.
        test(crate::ip::IPV6_MIN_MTU as usize, 1);
        test(crate::ip::IPV6_MIN_MTU as usize + 1, 0);
    }

    #[test]
    fn test_pending_frames() {
        let mut state =
            EthernetDeviceState::new(DUMMY_CONFIG_V4.local_mac, crate::ip::IPV6_MIN_MTU);
        let ip = IpAddr::V4(DUMMY_CONFIG_V4.local_ip);
        state.add_pending_frame(ip, vec![1]);
        state.add_pending_frame(ip, vec![2]);
        state.add_pending_frame(ip, vec![3]);

        // check that we're accumulating correctly...
        assert_eq!(3, state.take_pending_frames(ip).unwrap().count());
        // ...and that take_pending_frames clears all the buffered data.
        assert!(state.take_pending_frames(ip).is_none());

        for i in 0..ETHERNET_MAX_PENDING_FRAMES {
            assert!(state.add_pending_frame(ip, vec![i as u8]).is_none());
        }
        // check that adding more than capacity will drop the older buffers as
        // a proper FIFO queue.
        assert_eq!(0, state.add_pending_frame(ip, vec![255]).unwrap()[0]);
        assert_eq!(1, state.add_pending_frame(ip, vec![255]).unwrap()[0]);
        assert_eq!(2, state.add_pending_frame(ip, vec![255]).unwrap()[0]);
    }
}
