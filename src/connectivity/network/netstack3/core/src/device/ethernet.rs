// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Ethernet protocol.

use std::collections::{HashMap, HashSet, VecDeque};

use log::debug;
use net_types::ethernet::Mac;
use net_types::ip::{AddrSubnet, Ip, IpAddr, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use net_types::{BroadcastAddress, MulticastAddr, MulticastAddress, UnicastAddress};
use packet::{Buf, BufferMut, EmptyBuf, Nested, Serializer};
use specialize_ip_macro::specialize_ip_address;

use crate::device::arp::{self, ArpDevice, ArpHardwareType, ArpState};
use crate::device::ndp::{self, NdpState};
use crate::device::{DeviceId, FrameDestination, Tentative};
use crate::wire::arp::peek_arp_types;
use crate::wire::ethernet::{EthernetFrame, EthernetFrameBuilder};
use crate::{BufferDispatcher, Context, EventDispatcher, StackState};

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

/// Builder for [`EthernetDeviceState`].
pub(crate) struct EthernetDeviceStateBuilder {
    mac: Mac,
    mtu: u32,
    ndp_configs: ndp::NdpConfigurations,
}

impl EthernetDeviceStateBuilder {
    /// Create a new `EthernetDeviceStateBuilder`.
    pub(crate) fn new(mac: Mac, mtu: u32) -> Self {
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

        Self { mac, mtu, ndp_configs: ndp::NdpConfigurations::default() }
    }

    /// Update the NDP configurations that will be set on the ethernet
    /// device.
    pub(crate) fn set_ndp_configs(&mut self, v: ndp::NdpConfigurations) {
        self.ndp_configs = v;
    }

    /// Build the `EthernetDeviceState` from this builder.
    pub(crate) fn build(self) -> EthernetDeviceState {
        let mut ipv6_multicast_groups = HashSet::new();

        ipv6_multicast_groups.insert(self.mac.to_ipv6_link_local().to_solicited_node_address());

        // TODO(ghanan): Perform NDP's DAD on the link local address BEFORE receiving
        //               packets destined to it.

        EthernetDeviceState {
            mac: self.mac,
            mtu: self.mtu,
            ipv4_addr_sub: None,
            ipv6_addr_sub: None,
            ipv4_multicast_groups: HashSet::new(),
            ipv6_multicast_groups,
            ipv4_arp: ArpState::default(),
            ndp: NdpState::new(self.ndp_configs),
            pending_frames: HashMap::new(),
        }
    }
}

/// The state associated with an Ethernet device.
pub(crate) struct EthernetDeviceState {
    mac: Mac,
    mtu: u32,
    ipv4_addr_sub: Option<AddrSubnet<Ipv4Addr>>,
    ipv6_addr_sub: Option<Tentative<AddrSubnet<Ipv6Addr>>>,
    ipv4_multicast_groups: HashSet<MulticastAddr<Ipv4Addr>>,
    ipv6_multicast_groups: HashSet<MulticastAddr<Ipv6Addr>>,
    ipv4_arp: ArpState<Ipv4Addr, EthernetArpDevice>,
    ndp: ndp::NdpState<EthernetNdpDevice>,
    // pending_frames stores a list of serialized frames indexed by their
    // desintation IP addresses. The frames contain an entire EthernetFrame
    // body and the MTU check is performed before queueing them here.
    pending_frames: HashMap<IpAddr, VecDeque<Buf<Vec<u8>>>>,
}

impl EthernetDeviceState {
    /// Adds a pending frame `frame` associated with `local_addr` to the list
    /// of pending frames in the current device state.
    ///
    /// If an older frame had to be dropped because it exceeds the maximum
    /// allowed number of pending frames, it is returned.
    fn add_pending_frame(
        &mut self,
        local_addr: IpAddr,
        frame: Buf<Vec<u8>>,
    ) -> Option<Buf<Vec<u8>>> {
        let buff = self.pending_frames.entry(local_addr).or_insert_with(Default::default);
        buff.push_back(frame);
        if buff.len() > ETHERNET_MAX_PENDING_FRAMES {
            buff.pop_front()
        } else {
            None
        }
    }

    /// Takes all pending frames associated with address `local_addr`.
    fn take_pending_frames(
        &mut self,
        local_addr: IpAddr,
    ) -> Option<impl Iterator<Item = Buf<Vec<u8>>>> {
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
pub(crate) fn send_ip_frame<
    B: BufferMut,
    D: BufferDispatcher<B>,
    A: IpAddress,
    S: Serializer<Buffer = B>,
>(
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
            // The `serialize_vec_outer` call returns an `Either<B,
            // Buf<Vec<u8>>`. We could naively call `.as_ref().to_vec()` on it,
            // but if it were the `Buf<Vec<u8>>` variant, we'd be unnecessarily
            // allocating a new `Vec` when we already have one. Instead, we
            // leave the `Buf<Vec<u8>>` variant as it is, and only convert the
            // `B` variant by calling `map_a`. That gives us an
            // `Either<Buf<Vec<u8>>, Buf<Vec<u8>>`, which we call `into_inner`
            // on to get a `Buf<Vec<u8>>`.
            let frame = body
                .with_mtu(mtu as usize)
                .serialize_vec_outer()
                .map_err(|ser| ser.1.into_inner())?
                .map_a(|buffer| Buf::new(buffer.as_ref().to_vec(), ..))
                .into_inner();
            let dropped = state.add_pending_frame(local_addr, frame);
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
pub(crate) fn receive_frame<B: BufferMut, D: BufferDispatcher<B>>(
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
                    crate::device::arp::receive_arp_packet::<_, D, Ipv4Addr, EthernetArpDevice>(
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
    return match get_device_state(ctx.state(), device_id).ipv6_addr_sub.clone() {
        None => None,
        Some(a) => a.try_into_permanent(),
    };
}

/// Get the IP address and subnet associated with this device, including tentative
/// addresses.
#[specialize_ip_address]
pub(crate) fn get_ip_addr_subnet_with_tentative<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: usize,
) -> Option<Tentative<AddrSubnet<A>>> {
    #[ipv4addr]
    return get_device_state(ctx.state(), device_id)
        .ipv4_addr_sub
        .map(|x| Tentative::new_permanent(x));

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
    {
        let old_addr = get_device_state_mut(ctx.state_mut(), device_id).ipv6_addr_sub.take();

        if let Some(ref addr) = old_addr {
            if addr.is_tentative() {
                // Cancel current duplicate address detection for `addr` because we
                // will assign a new address to the device.
                //
                // `cancel_duplicate_address_detection` may panic if we are not
                // performing DAD on `addr`. However, we will only reach here
                // if `addr` is marked as tentative. If `addr` is marked as
                // tentative, then we know that we are performing DAD on it.
                // Given this, we know `cancel_duplicate_address_detection` will
                // not panic.
                ndp::cancel_duplicate_address_detection::<_, EthernetNdpDevice>(
                    ctx,
                    device_id,
                    addr.inner().addr(),
                );
            }

            // Leave the solicited-node multicast group for the previous address.
            let addr = addr.inner().addr().to_solicited_node_address();
            leave_ip_multicast(ctx, device_id, addr);
        }

        // First, join the solicited-node multicast group.
        join_ip_multicast(ctx, device_id, addr_sub.addr().to_solicited_node_address());

        let device_state = get_device_state_mut(ctx.state_mut(), device_id);
        device_state.ipv6_addr_sub = Some(Tentative::new_tentative(addr_sub));
        ndp::start_duplicate_address_detection::<D, EthernetNdpDevice>(
            ctx,
            device_id,
            addr_sub.addr(),
        );
    }
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

    fn send_arp_frame<B: BufferMut, D: BufferDispatcher<B>, S: Serializer<Buffer = B>>(
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
    ) -> Option<Tentative<Ipv6Addr>> {
        let state = get_device_state(state, device_id);
        // TODO(brunodalbo) just returning either the configured or EUI
        //  link_local address for now, we need a better structure to keep
        //  a list of IPv6 addresses.
        match state.ipv6_addr_sub {
            Some(addr_sub) => Some(addr_sub.map(|a| a.into_addr())),
            None => Some(Tentative::new_permanent(state.mac.to_ipv6_link_local())),
        }
    }

    fn ipv6_addr_state<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
        address: &Ipv6Addr,
    ) -> ndp::AddressState {
        let state = get_device_state(state, device_id);

        if let Some(addr) = state.ipv6_addr_sub {
            if addr.inner().addr() == *address {
                if addr.is_tentative() {
                    return ndp::AddressState::Tentative;
                } else {
                    return ndp::AddressState::Assigned;
                }
            }
        }

        if state.mac.to_ipv6_link_local() == *address {
            // TODO(ghanan): perform DAD on link local address instead of assuming
            //               it is safe to assign.
            ndp::AddressState::Assigned;
        }

        ndp::AddressState::Unassigned
    }

    fn send_ipv6_frame_to<D: EventDispatcher, S: Serializer<Buffer = EmptyBuf>>(
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

    fn send_ipv6_frame<D: EventDispatcher, S: Serializer<Buffer = EmptyBuf>>(
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

    fn duplicate_address_detected<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: usize,
        addr: Ipv6Addr,
    ) {
        let state = get_device_state_mut(ctx.state_mut(), device_id);
        state.ipv6_addr_sub = None;

        leave_ip_multicast(ctx, device_id, addr.to_solicited_node_address());

        // TODO: we need to pick a different address depending on what flow we are using.
    }

    fn unique_address_determined<D: EventDispatcher>(
        state: &mut StackState<D>,
        device_id: usize,
        addr: Ipv6Addr,
    ) {
        let ipv6_addr_sub = &mut get_device_state_mut(state, device_id).ipv6_addr_sub;
        match ipv6_addr_sub {
            Some(ref mut tentative) => {
                tentative.mark_permanent();
                assert_eq!(tentative.inner().into_addr(), addr);
            }
            _ => panic!("Attempted to resolve an unknown tentative address"),
        }
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
                frame.encapsulate(EthernetFrameBuilder::new(src_mac, dst_mac, ether_type)),
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
            EthernetDeviceStateBuilder::new(DUMMY_CONFIG_V4.local_mac, crate::ip::IPV6_MIN_MTU)
                .build();
        let ip = IpAddr::V4(DUMMY_CONFIG_V4.local_ip);
        state.add_pending_frame(ip, Buf::new(vec![1], ..));
        state.add_pending_frame(ip, Buf::new(vec![2], ..));
        state.add_pending_frame(ip, Buf::new(vec![3], ..));

        // check that we're accumulating correctly...
        assert_eq!(3, state.take_pending_frames(ip).unwrap().count());
        // ...and that take_pending_frames clears all the buffered data.
        assert!(state.take_pending_frames(ip).is_none());

        for i in 0..ETHERNET_MAX_PENDING_FRAMES {
            assert!(state.add_pending_frame(ip, Buf::new(vec![i as u8], ..)).is_none());
        }
        // check that adding more than capacity will drop the older buffers as
        // a proper FIFO queue.
        assert_eq!(0, state.add_pending_frame(ip, Buf::new(vec![255], ..)).unwrap().as_ref()[0]);
        assert_eq!(1, state.add_pending_frame(ip, Buf::new(vec![255], ..)).unwrap().as_ref()[0]);
        assert_eq!(2, state.add_pending_frame(ip, Buf::new(vec![255], ..)).unwrap().as_ref()[0]);
    }
}
