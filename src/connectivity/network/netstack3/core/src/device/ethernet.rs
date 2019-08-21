// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Ethernet protocol.

use std::collections::{HashMap, HashSet, VecDeque};
use std::iter::FilterMap;
use std::num::NonZeroU8;
use std::slice::Iter;

use log::{debug, trace};
use net_types::ethernet::Mac;
use net_types::ip::{AddrSubnet, Ip, IpAddr, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use net_types::{
    BroadcastAddress, LinkLocalAddr, MulticastAddr, MulticastAddress, SpecifiedAddr, UnicastAddress,
};
use packet::{Buf, BufferMut, EmptyBuf, Nested, Serializer};
use specialize_ip_macro::{specialize_ip, specialize_ip_address};

use crate::context::{FrameContext, StateContext, TimerContext};
use crate::device::arp::{
    self, ArpContext, ArpFrameMetadata, ArpHardwareType, ArpState, ArpTimerId,
};
use crate::device::ndp::{self, NdpState};
use crate::device::{
    is_device_initialized, AddressEntry, AddressError, AddressState, DeviceId, DeviceLayerTimerId,
    FrameDestination, Tentative,
};
use crate::wire::arp::peek_arp_types;
use crate::wire::ethernet::{EthernetFrame, EthernetFrameBuilder};
use crate::{BufferDispatcher, Context, EventDispatcher, StackState, TimerId, TimerIdInner};

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
    route_ipv4: bool,
    route_ipv6: bool,
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

        Self {
            mac,
            mtu,
            route_ipv4: false,
            route_ipv6: false,
            ndp_configs: ndp::NdpConfigurations::default(),
        }
    }

    /// Update the NDP configurations that will be set on the ethernet
    /// device.
    pub(crate) fn set_ndp_configs(&mut self, v: ndp::NdpConfigurations) {
        self.ndp_configs = v;
    }

    /// Enable/disable IP packet routing.
    pub(crate) fn set_route<I: Ip>(&mut self, v: bool) {
        // We implement this method by using an inner function that gets specialized because when
        // `specialize_ip_macro` generates the IP specific code, it doesn't properly handle a type's
        // member functions. Instead lets say we have the following code:
        //
        // ```
        // #[specialize_ip]
        // pub(crate) fn set_route<I: Ip>(&mut self, v: bool) {
        //     #[ipv4]
        //     self.route_ipv4 = v;
        //
        //     #[ipv6]
        //     self.route_ipv6 = v;
        // }
        // ```
        //
        // After `specialize_ip_macro` goes in and does its magic, we end up with the following
        // (code irrelevant to this example has been replaced with "..."):
        //
        // ```
        // pub(crate) fn set_route<I: Ip>(&mut self, v: bool) {
        //     trait Ext: net_types::ip::Ip {
        //         ...
        //         fn f(&mut self, v: bool);
        //         ...
        //     }
        //
        //     ...
        //
        //     impl Ext for net_types::ip::Ipv4 {
        //         ...
        //         fn f(&mut self, v: bool) { self.route_ipv4 = v; }
        //         ...
        //     }
        //
        //     impl Ext for net_types::ip::Ipv6 {
        //         ...
        //         fn f(&mut self, v: bool) { self.route_ipv6 = v; }
        //         ...
        //     }
        //
        //     I::f::<>(self, v)
        // }
        // ```
        //
        // Here we can see that the generated functions still use `&mut self` but in its context,
        // `self` will refer to the `Ip` object instead of the original `self`, the
        // `EthernetDeviceStateBuilder`. Having an inner function that has no `&mut self` arguments
        // allows us to workaround this issue.
        //
        // TODO(ghanan): Use the desided code mentioned above once this bug is fixed.
        #[specialize_ip]
        fn inner<I: Ip>(builder: &mut EthernetDeviceStateBuilder, v: bool) {
            #[ipv4]
            builder.route_ipv4 = v;

            #[ipv6]
            builder.route_ipv6 = v;
        }

        inner::<I>(self, v)
    }

    /// Build the `EthernetDeviceState` from this builder.
    pub(crate) fn build<D: EventDispatcher>(self) -> EthernetDeviceState<D> {
        let solicited_node_link_local_addr =
            self.mac.to_ipv6_link_local().get().to_solicited_node_address();

        let mut ipv6_multicast_groups = HashSet::new();
        ipv6_multicast_groups.insert(solicited_node_link_local_addr);

        let mut link_multicast_groups = HashSet::new();
        link_multicast_groups.insert(MulticastAddr::from(&solicited_node_link_local_addr));

        // TODO(ghanan): Perform NDP's DAD on the link local address BEFORE receiving
        //               packets destined to it.

        EthernetDeviceState {
            mac: self.mac,
            mtu: self.mtu,
            hw_mtu: self.mtu,
            ipv6_hop_limit: ndp::HOP_LIMIT_DEFAULT,
            ipv4_addr_sub: Vec::new(),
            ipv6_addr_sub: Vec::new(),
            ipv4_multicast_groups: HashSet::new(),
            ipv6_multicast_groups,
            link_multicast_groups,
            ipv4_arp: ArpState::default(),
            ndp: NdpState::new(self.ndp_configs),
            route_ipv4: self.route_ipv4,
            route_ipv6: self.route_ipv6,
            pending_frames: HashMap::new(),
            promiscuous_mode: false,
        }
    }
}

/// The state associated with an Ethernet device.
pub(crate) struct EthernetDeviceState<D: EventDispatcher> {
    /// Mac address of the device this state is for.
    mac: Mac,

    /// The value this netstack assumes as the device's current MTU.
    mtu: u32,

    /// The maximum MTU allowed by the hardware.
    ///
    /// `mtu` MUST NEVER be greater than `hw_mtu`.
    hw_mtu: u32,

    /// Default hop limit for new IPv6 packets sent from this device.
    // TODO(ghanan): Once we separate out device-IP state from device-specific
    //               state, move this to some IPv6-device state.
    ipv6_hop_limit: NonZeroU8,

    /// Assigned IPv4 addresses.
    ipv4_addr_sub: Vec<AddressEntry<Ipv4Addr>>,

    /// Assigned IPv6 addresses.
    ///
    /// May be tentative (performing NDP's Duplicate Address Detection).
    ipv6_addr_sub: Vec<AddressEntry<Ipv6Addr>>,

    /// IPv4 multicast groups this device has joined.
    ipv4_multicast_groups: HashSet<MulticastAddr<Ipv4Addr>>,

    /// IPv6 multicast groups this device has joined.
    ipv6_multicast_groups: HashSet<MulticastAddr<Ipv6Addr>>,

    /// Link multicast groups this device has joined.
    link_multicast_groups: HashSet<MulticastAddr<Mac>>,

    /// IPv4 ARP state.
    ipv4_arp: ArpState<Ipv4Addr, Mac>,

    /// (IPv6) NDP state.
    ndp: ndp::NdpState<EthernetNdpDevice, D>,

    /// A flag indicating whether routing of IPv4 packets not destined for this device is
    /// enabled.
    ///
    /// This flag controls whether or not packets can be routed from this device. That is, when a
    /// packet arrives at a device it is not destined for, the packet can only be routed if the
    /// device it arrived at has routing enabled and there exists another device that has a path
    /// to the packet's destination, regardless the other device's routing ability.
    ///
    /// Default: `false`.
    route_ipv4: bool,

    /// A flag indicating whether routing of IPv6 packets not destined for this device is
    /// enabled.
    ///
    /// This flag controls whether or not packets can be routed from this device. That is, when a
    /// packet arrives at a device it is not destined for, the packet can only be routed if the
    /// device it arrived at has routing enabled and there exists another device that has a path
    /// to the packet's destination, regardless the other device's routing ability.
    ///
    /// Default: `false`.
    route_ipv6: bool,

    // pending_frames stores a list of serialized frames indexed by their
    // desintation IP addresses. The frames contain an entire EthernetFrame
    // body and the MTU check is performed before queueing them here.
    pending_frames: HashMap<IpAddr, VecDeque<Buf<Vec<u8>>>>,

    /// A flag indicating whether the device will accept all ethernet frames that it receives,
    /// regardless of the ethernet frame's destination MAC address.
    promiscuous_mode: bool,
}

impl<D: EventDispatcher> EthernetDeviceState<D> {
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

    /// Is a packet with a destination MAC address, `dst`, destined for this device?
    ///
    /// Returns `true` if this device is has `dst_mac` as its assigned MAC address, `dst_mac` is the
    /// broadcast MAC address, or it is one of the multicast MAC addresses the device has joined.
    fn should_accept(&self, dst_mac: &Mac) -> bool {
        (self.mac == *dst_mac)
            || dst_mac.is_broadcast()
            || (MulticastAddr::new(*dst_mac)
                .map(|a| self.link_multicast_groups.contains(&a))
                .unwrap_or(false))
    }

    /// Should a packet with destination MAC address, `dst`, be accepted by this device?
    ///
    /// Returns `true` if this device is in promiscuous mode or the frame is destined for this
    /// device.
    fn should_deliver(&self, dst_mac: &Mac) -> bool {
        self.promiscuous_mode || self.should_accept(dst_mac)
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
    local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    trace!("ethernet::send_ip_frame: local_addr = {:?}; device = {:?}", local_addr, device_id);

    let state = get_device_state_mut(ctx.state_mut(), device_id);
    let (local_mac, mtu) = (state.mac, state.mtu);

    let local_addr = local_addr.get();
    let dst_mac = match MulticastAddr::new(local_addr) {
        Some(multicast) => Ok(Mac::from(&multicast)),
        None => {
            #[ipv4addr]
            {
                arp::lookup(ctx, device_id, local_mac, local_addr).ok_or(IpAddr::V4(local_addr))
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
    trace!("ethernet::receive_frame: device_id = {:?}", device_id);
    let frame = if let Ok(frame) = buffer.parse::<EthernetFrame<_>>() {
        frame
    } else {
        trace!("ethernet::receive_frame: failed to parse ethernet frame");
        // TODO(joshlf): Do something else?
        return;
    };

    let (src, dst) = (frame.src_mac(), frame.dst_mac());
    let device = DeviceId::new_ethernet(device_id);

    if !get_device_state(ctx.state(), device_id).should_deliver(&dst) {
        trace!("ethernet::receive_frame: destination mac {:?} not for device {:?}", dst, device_id);
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
                    crate::device::arp::receive_arp_packet(ctx, device_id, buffer)
                }
                types => debug!("got ARP packet for unsupported types: {:?}", types),
            }
        }
        Some(EtherType::Ipv4) => crate::ip::receive_ipv4_packet(ctx, device, frame_dst, buffer),
        Some(EtherType::Ipv6) => crate::ip::receive_ipv6_packet(ctx, device, frame_dst, buffer),
        Some(EtherType::Other(_)) | None => {} // TODO(joshlf)
    }
}

/// Set the promiscuous mode flag on `device_id`.
pub(crate) fn set_promiscuous_mode<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: usize,
    enabled: bool,
) {
    get_device_state_mut(ctx.state_mut(), device_id).promiscuous_mode = enabled;
}

/// Get a single IP address for a device.
///
/// Note, tentative IP addresses (addresses which are not yet fully bound to a
/// device) will not be returned by `get_ip_addr`.
#[specialize_ip_address]
pub(crate) fn get_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    state: &StackState<D>,
    device_id: usize,
) -> Option<AddrSubnet<A>> {
    get_ip_addr_subnets(state, device_id).nth(0)
}

/// Get the IP address and subnet pais associated with this device.
///
/// Note, tentative IP addresses (addresses which are not yet fully bound to a
/// device) will not be returned by `get_ip_addr_subnets`.
///
/// Returns an [`Iterator`] of `AddrSubnet<A>`.
///
/// See [`Tentative`] and [`AddrSubnet`] for more information.
#[specialize_ip_address]
pub(crate) fn get_ip_addr_subnets<D: EventDispatcher, A: IpAddress>(
    state: &StackState<D>,
    device_id: usize,
) -> FilterMap<Iter<AddressEntry<A>>, fn(&AddressEntry<A>) -> Option<AddrSubnet<A>>> {
    let state = get_device_state(state, device_id);

    #[ipv4addr]
    let addresses = &state.ipv4_addr_sub;

    #[ipv6addr]
    let addresses = &state.ipv6_addr_sub;

    addresses.iter().filter_map(
        |a| {
            if a.state().is_assigned() {
                Some(*a.addr_sub())
            } else {
                None
            }
        },
    )
}

/// Get the IP address and subnet associated with this device, including tentative
/// addresses.
///
/// Returns an [`Iterator`] of `Tentative<AddrSubnet<A>>`.
///
/// See [`Tentative`] and [`AddrSubnet`] for more information.
#[specialize_ip_address]
pub(crate) fn get_ip_addr_subnets_with_tentative<D: EventDispatcher, A: IpAddress>(
    state: &StackState<D>,
    device_id: usize,
) -> FilterMap<Iter<AddressEntry<A>>, fn(&AddressEntry<A>) -> Option<Tentative<AddrSubnet<A>>>> {
    let state = get_device_state(state, device_id);

    #[ipv4addr]
    let addresses = &state.ipv4_addr_sub;

    #[ipv6addr]
    let addresses = &state.ipv6_addr_sub;

    addresses.iter().filter_map(|a| {
        if a.state().is_assigned() {
            Some(Tentative::new_permanent(*a.addr_sub()))
        } else if a.state().is_tentative() {
            Some(Tentative::new_tentative(*a.addr_sub()))
        } else {
            None
        }
    })
}

/// Get the state of an address on a device.
///
/// Returns `None` if `addr` is not associated with `device_id`.
#[specialize_ip_address]
pub fn get_ip_addr_state<D: EventDispatcher, A: IpAddress>(
    state: &StackState<D>,
    device_id: usize,
    addr: &SpecifiedAddr<A>,
) -> Option<AddressState> {
    let state = get_device_state(state, device_id);

    #[ipv4addr]
    let addresses = &state.ipv4_addr_sub;

    #[ipv6addr]
    let addresses = &state.ipv6_addr_sub;

    addresses.iter().find_map(|a| if a.addr_sub().addr() == *addr { Some(a.state()) } else { None })
}

/// Adds an IP address and associated subnet to this device.
#[specialize_ip_address]
pub(crate) fn add_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: usize,
    addr_sub: AddrSubnet<A>,
) -> Result<(), AddressError> {
    let addr = addr_sub.addr();

    if get_ip_addr_state(ctx.state(), device_id, &addr).is_some() {
        return Err(AddressError::AlreadyExists);
    }

    let state = get_device_state_mut(ctx.state_mut(), device_id);

    #[ipv4addr]
    state.ipv4_addr_sub.push(AddressEntry::new(addr_sub, AddressState::Assigned));

    #[ipv6addr]
    {
        // First, join the solicited-node multicast group.
        join_ip_multicast(ctx, device_id, addr.to_solicited_node_address());

        let state = get_device_state_mut(ctx.state_mut(), device_id);
        state.ipv6_addr_sub.push(AddressEntry::new(addr_sub, AddressState::Tentative));
        ndp::start_duplicate_address_detection::<D, EthernetNdpDevice>(ctx, device_id, addr.get());
    }

    Ok(())
}

/// Removes an IP address and associated subnet from this device.
#[specialize_ip_address]
pub(crate) fn del_ip_addr<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: usize,
    addr: &SpecifiedAddr<A>,
) -> Result<(), AddressError> {
    #[ipv4addr]
    {
        let state = get_device_state_mut(ctx.state_mut(), device_id);

        let original_size = state.ipv4_addr_sub.len();
        state.ipv4_addr_sub.retain(|x| x.addr_sub().addr() != *addr);
        let new_size = state.ipv4_addr_sub.len();

        if new_size == original_size {
            return Err(AddressError::NotFound);
        }

        assert_eq!(original_size - new_size, 1);

        Ok(())
    }

    #[ipv6addr]
    {
        if let Some(state) = get_ip_addr_state(ctx.state(), device_id, addr) {
            if state.is_tentative() {
                // Cancel current duplicate address detection for `addr` as we are
                // removing this IP.
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
                    addr.get(),
                );
            }
        } else {
            return Err(AddressError::NotFound);
        }

        let state = get_device_state_mut(ctx.state_mut(), device_id);

        let original_size = state.ipv6_addr_sub.len();
        state.ipv6_addr_sub.retain(|x| x.addr_sub().addr() != *addr);
        let new_size = state.ipv6_addr_sub.len();

        // Since we just checked earlier if we had the address, we must have removed it
        // now.
        assert_eq!(original_size - new_size, 1);

        // Leave the the solicited-node multicast group.
        leave_ip_multicast(ctx, device_id, addr.to_solicited_node_address());

        Ok(())
    }
}

/// Get the IPv6 link-local address associated with this device.
///
/// The IPv6 link-local address returned is constructed from this device's MAC
/// address.
pub(crate) fn get_ipv6_link_local_addr<D: EventDispatcher>(
    ctx: &Context<D>,
    device_id: usize,
) -> Option<LinkLocalAddr<Ipv6Addr>> {
    // TODO(brunodalbo) the link local address is subject to the same collision
    //  verifications as prefix global addresses, we should keep a state machine
    //  about that check and cache the adopted address. For now, we just compose
    //  the link-local from the ethernet MAC.
    Some(get_device_state(ctx.state(), device_id).mac.to_ipv6_link_local())
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
    let device_state = get_device_state_mut(ctx.state_mut(), device_id);
    let mac = MulticastAddr::from(&multicast_addr);

    trace!(
        "ethernet::join_ip_multicast: joining IP multicast {:?} and MAC multicast {:?}",
        multicast_addr,
        mac
    );

    #[ipv4addr]
    device_state.ipv4_multicast_groups.insert(multicast_addr);

    #[ipv6addr]
    device_state.ipv6_multicast_groups.insert(multicast_addr);

    // TODO(ghanan): Make `EventDispatcher` aware of this to maintain a single source of truth.
    device_state.link_multicast_groups.insert(mac);
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
    let device_state = get_device_state_mut(ctx.state_mut(), device_id);
    let mac = MulticastAddr::from(&multicast_addr);

    trace!(
        "ethernet::leave_ip_multicast: leaving IP multicast {:?} and MAC multicast {:?}",
        multicast_addr,
        mac
    );

    // TODO(ghanan): Make `EventDispatcher` aware of this to maintain a single source of truth.
    device_state.link_multicast_groups.remove(&mac);

    #[ipv4addr]
    device_state.ipv4_multicast_groups.remove(&multicast_addr);

    #[ipv6addr]
    device_state.ipv6_multicast_groups.remove(&multicast_addr);
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
pub(crate) fn get_mtu<D: EventDispatcher>(state: &StackState<D>, device_id: usize) -> u32 {
    get_device_state(state, device_id).mtu
}

/// Get the hop limit for new IPv6 packets that will be sent out from `device_id`.
pub(crate) fn get_ipv6_hop_limit<D: EventDispatcher>(
    ctx: &Context<D>,
    device_id: usize,
) -> NonZeroU8 {
    get_device_state(ctx.state(), device_id).ipv6_hop_limit
}

/// Is IP packet routing enabled on `device_id`?
///
/// Note, `true` does not necessarily mean that `device` is currently routing IP packets. It
/// only means that `device` is allowed to route packets. To route packets, this netstack must
/// be configured to allow IP packets to be routed if it was not destined for this node.
#[specialize_ip]
pub(crate) fn is_routing_enabled<D: EventDispatcher, I: Ip>(
    ctx: &Context<D>,
    device_id: usize,
) -> bool {
    #[ipv4]
    return get_device_state(ctx.state(), device_id).route_ipv4;

    #[ipv6]
    return get_device_state(ctx.state(), device_id).route_ipv6;
}

/// Sets the IP packet routing flag on `device_id`.
///
/// This method MUST NOT be called directly. It MUST only only called by
/// [`crate::device::set_routing_enabled`].
///
/// See [`crate::device::set_routing_enabled`] for more information.
#[specialize_ip]
pub(super) fn set_routing_enabled_inner<D: EventDispatcher, I: Ip>(
    ctx: &mut Context<D>,
    device_id: usize,
    enabled: bool,
) {
    let state = get_device_state_mut(ctx.state_mut(), device_id);

    #[ipv4]
    state.route_ipv4 = enabled;

    #[ipv6]
    state.route_ipv6 = enabled;
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
    crate::device::arp::insert_static(ctx, device_id, addr, mac);
}

/// Insert an entry into this device's NDP table.
///
/// This method only gets called when testing to force set a neighbor's
/// link address so that lookups succeed immediately, without doing
/// address resolution.
#[cfg(test)]
pub(crate) fn insert_ndp_table_entry<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: usize,
    addr: Ipv6Addr,
    mac: Mac,
) {
    ndp::insert_neighbor::<D, EthernetNdpDevice>(ctx, device_id, addr, mac)
}

/// Deinitializes and cleans up state for ethernet devices
///
/// After this function is called, the ethernet device should not be used and
/// nothing else should be done with the state.
pub(crate) fn deinitialize<D: EventDispatcher>(ctx: &mut Context<D>, device_id: usize) {
    crate::device::arp::deinitialize(ctx, device_id);
    crate::device::ndp::deinitialize(ctx, device_id);
}

fn get_device_state_mut<D: EventDispatcher>(
    state: &mut StackState<D>,
    device_id: usize,
) -> &mut EthernetDeviceState<D> {
    // TODO(joshlf): Sometimes we want lookups to be infallible (if we know that
    // the device exists), but sometimes we want to report an error to the user.
    // Right now, this is a DoS vector.
    state
        .device
        .ethernet
        .get_mut(device_id)
        .unwrap_or_else(|| panic!("no such Ethernet device: {}", device_id))
        .device_mut()
}

fn get_device_state<D: EventDispatcher>(
    state: &StackState<D>,
    device_id: usize,
) -> &EthernetDeviceState<D> {
    // TODO(joshlf): Sometimes we want lookups to be infallible (if we know that
    // the device exists), but sometimes we want to report an error to the user.
    // Right now, this is a DoS vector.
    state
        .device
        .ethernet
        .get(device_id)
        .unwrap_or_else(|| panic!("no such Ethernet device: {}", device_id))
        .device()
}

impl<D: EventDispatcher> StateContext<usize, ArpState<Ipv4Addr, Mac>> for Context<D> {
    fn get_state(&self, id: usize) -> &ArpState<Ipv4Addr, Mac> {
        &get_device_state(self.state(), id).ipv4_arp
    }

    fn get_state_mut(&mut self, id: usize) -> &mut ArpState<Ipv4Addr, Mac> {
        &mut get_device_state_mut(self.state_mut(), id).ipv4_arp
    }
}

impl<D: EventDispatcher> TimerContext<ArpTimerId<usize, Ipv4Addr>> for Context<D> {
    fn schedule_timer_instant(
        &mut self,
        time: D::Instant,
        id: ArpTimerId<usize, Ipv4Addr>,
    ) -> Option<D::Instant> {
        self.dispatcher_mut()
            .schedule_timeout_instant(time, TimerId::from(DeviceLayerTimerId::from(id)))
    }

    fn cancel_timer(&mut self, id: ArpTimerId<usize, Ipv4Addr>) -> Option<D::Instant> {
        self.dispatcher_mut().cancel_timeout(TimerId::from(DeviceLayerTimerId::from(id.clone())))
    }

    fn cancel_timers_with<F: FnMut(&ArpTimerId<usize, Ipv4Addr>) -> bool>(&mut self, mut f: F) {
        self.dispatcher_mut().cancel_timeouts_with(|id| match id {
            TimerId(TimerIdInner::DeviceLayer(DeviceLayerTimerId::ArpIpv4(id))) => f(id),
            _ => false,
        })
    }

    fn scheduled_instant(&self, id: ArpTimerId<usize, Ipv4Addr>) -> Option<D::Instant> {
        self.dispatcher().scheduled_instant(TimerId::from(DeviceLayerTimerId::from(id)))
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> FrameContext<B, ArpFrameMetadata<usize, Mac>>
    for Context<D>
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        meta: ArpFrameMetadata<usize, Mac>,
        body: S,
    ) -> Result<(), S> {
        let src = get_device_state(self.state(), meta.device_id).mac;
        self.dispatcher_mut()
            .send_frame(
                DeviceId::new_ethernet(meta.device_id),
                body.encapsulate(EthernetFrameBuilder::new(src, meta.dst_addr, EtherType::Arp)),
            )
            .map_err(Nested::into_inner)
    }
}

impl<D: EventDispatcher> ArpContext<Ipv4Addr, Mac> for Context<D> {
    type DeviceId = usize;

    fn get_protocol_addr(&self, device_id: usize) -> Option<Ipv4Addr> {
        get_ip_addr_subnet::<_, Ipv4Addr>(self.state(), device_id).map(|a| a.addr().get())
    }

    fn get_hardware_addr(&self, device_id: usize) -> Mac {
        get_device_state(self.state(), device_id).mac
    }

    fn address_resolved(&mut self, device_id: usize, proto_addr: Ipv4Addr, hw_addr: Mac) {
        mac_resolved(self, device_id, IpAddr::V4(proto_addr), hw_addr);
    }

    fn address_resolution_failed(&mut self, device_id: usize, proto_addr: Ipv4Addr) {
        mac_resolution_failed(self, device_id, IpAddr::V4(proto_addr));
    }
}

/// Dummy type used to implement NdpDevice
pub(crate) struct EthernetNdpDevice;

impl ndp::NdpDevice for EthernetNdpDevice {
    type LinkAddress = Mac;

    fn get_ndp_state_mut<D: EventDispatcher>(
        state: &mut StackState<D>,
        device_id: usize,
    ) -> &mut ndp::NdpState<Self, D> {
        &mut get_device_state_mut(state, device_id).ndp
    }

    fn get_ndp_state<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
    ) -> &ndp::NdpState<Self, D> {
        &get_device_state(state, device_id).ndp
    }

    fn get_link_layer_addr<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
    ) -> Self::LinkAddress {
        get_device_state(state, device_id).mac
    }

    fn get_link_local_addr<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
    ) -> Option<Tentative<Ipv6Addr>> {
        let state = get_device_state(state, device_id);
        Some(Tentative::new_permanent(state.mac.to_ipv6_link_local().get()))
    }

    fn get_ipv6_addr<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
    ) -> Option<Ipv6Addr> {
        // Return a non tentative global address, or the link-local address if no non-tentative
        // global addressses are associated with `device_id`.

        match get_ip_addr_subnet::<_, Ipv6Addr>(state, device_id) {
            Some(addr_sub) => Some(addr_sub.addr().get()),
            None => Some(get_device_state(state, device_id).mac.to_ipv6_link_local().get()),
        }
    }

    fn ipv6_addr_state<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
        address: &Ipv6Addr,
    ) -> Option<AddressState> {
        let address = SpecifiedAddr::new(*address)?;

        if let Some(state) = get_ip_addr_state::<_, Ipv6Addr>(state, device_id, &address) {
            Some(state)
        } else if get_device_state(state, device_id).mac.to_ipv6_link_local().get() == *address {
            // TODO(ghanan): perform DAD on link local address instead of assuming
            //               it is safe to assign.
            Some(AddressState::Assigned)
        } else {
            None
        }
    }

    fn send_ipv6_frame<D: EventDispatcher, S: Serializer<Buffer = EmptyBuf>>(
        ctx: &mut Context<D>,
        device_id: usize,
        next_hop: Ipv6Addr,
        body: S,
    ) -> Result<(), S> {
        // `device_id` must be initialized.
        assert!(is_device_initialized(ctx.state(), Self::get_device_id(device_id)));

        // TODO(joshlf): Wire `SpecifiedAddr` through the `ndp` module.
        send_ip_frame(ctx, device_id, SpecifiedAddr::new(next_hop).unwrap(), body)
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

        let original_size = state.ipv6_addr_sub.len();
        state.ipv6_addr_sub.retain(|x| x.addr_sub().addr().get() != addr);
        let new_size = state.ipv6_addr_sub.len();

        // We must have removed the address.
        assert_eq!(original_size - new_size, 1);

        // Leave the the solicited-node multicast group.
        leave_ip_multicast(ctx, device_id, addr.to_solicited_node_address());

        // TODO: we need to pick a different address depending on what flow we are using.
    }

    fn unique_address_determined<D: EventDispatcher>(
        state: &mut StackState<D>,
        device_id: usize,
        addr: Ipv6Addr,
    ) {
        trace!(
            "ethernet::unique_address_determined: device_id = {:?}; addr = {:?}",
            device_id,
            addr
        );

        if let Some(entry) = get_device_state_mut(state, device_id)
            .ipv6_addr_sub
            .iter_mut()
            .find(|a| a.addr_sub().addr().get() == addr)
        {
            entry.mark_permanent();
        } else {
            panic!("Attempted to resolve an unknown tentative address");
        }
    }

    fn set_mtu<D: EventDispatcher>(state: &mut StackState<D>, device_id: usize, mut mtu: u32) {
        // TODO(ghanan): Should this new MTU be updated only from the netstack's perspective or
        //               be exposed to the device hardware?

        // `mtu` must not be less than the minimum IPv6 MTU.
        assert!(mtu >= crate::ip::path_mtu::IPV6_MIN_MTU);

        let dev_state = get_device_state_mut(state, device_id);

        // If `mtu` is greater than what the device supports, set `mtu` to the maximum MTU the
        // device supports.
        if mtu > dev_state.hw_mtu {
            trace!("ethernet::ndp_device::set_mtu: MTU of {:?} is greater than the device {:?}'s max MTU of {:?}, using device's max MTU instead", mtu, device_id, dev_state.hw_mtu);
            mtu = dev_state.hw_mtu;
        }

        trace!("ethernet::ndp_device::set_mtu: setting link MTU to {:?}", mtu);
        dev_state.mtu = mtu;
    }

    fn set_hop_limit<D: EventDispatcher>(
        state: &mut StackState<D>,
        device_id: usize,
        hop_limit: NonZeroU8,
    ) {
        get_device_state_mut(state, device_id).ipv6_hop_limit = hop_limit;
    }

    fn is_router<D: EventDispatcher>(ctx: &Context<D>, device_id: usize) -> bool {
        crate::device::is_router_device::<_, Ipv6>(ctx, Self::get_device_id(device_id))
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
    use rand::Rng;
    use specialize_ip_macro::specialize_ip;

    use super::*;
    use crate::device::{is_routing_enabled, set_routing_enabled};
    use crate::ip::{
        dispatch_receive_ip_packet_name, receive_ip_packet, IpExt, IpPacketBuilder, IpProto,
        IPV6_MIN_MTU,
    };
    use crate::testutil::{
        add_arp_or_ndp_table_entry, get_counter_val, get_dummy_config, get_other_ip_address,
        new_rng, parse_icmp_packet_in_ip_packet_in_ethernet_frame,
        parse_ip_packet_in_ethernet_frame, DummyEventDispatcher, DummyEventDispatcherBuilder,
        DUMMY_CONFIG_V4,
    };
    use crate::wire::icmp::{IcmpDestUnreachable, IcmpIpExt, IcmpMessage};
    use crate::wire::testdata::{dns_request_v4, dns_request_v6};
    use crate::StackStateBuilder;

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
        let mut state = EthernetDeviceStateBuilder::new(DUMMY_CONFIG_V4.local_mac, IPV6_MIN_MTU)
            .build::<DummyEventDispatcher>();
        let ip = IpAddr::V4(DUMMY_CONFIG_V4.local_ip.into_addr());
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

    #[specialize_ip]
    fn test_receive_ip_frame<I: Ip>(initialize: bool) {
        //
        // Should only receive a frame if the device is initialized
        //

        let config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::default().build::<DummyEventDispatcher>();
        let device = ctx.state_mut().add_ethernet_device(config.local_mac, crate::ip::IPV6_MIN_MTU);

        #[ipv4]
        let mut bytes = dns_request_v4::ETHERNET_FRAME.bytes.to_vec();

        #[ipv6]
        let mut bytes = dns_request_v6::ETHERNET_FRAME.bytes.to_vec();

        let mac_bytes = config.local_mac.bytes();
        bytes[0..6].copy_from_slice(&mac_bytes);

        if initialize {
            crate::device::initialize_device(&mut ctx, device);
        }

        // Will panic if we do not initialize.
        crate::device::receive_frame(&mut ctx, device, Buf::new(bytes, ..));

        // If we did not initialize, we would not reach here since
        // `receive_frame` would have paniced.
        #[ipv4]
        assert_eq!(get_counter_val(&mut ctx, "receive_ipv4_packet"), 1);
        #[ipv6]
        assert_eq!(get_counter_val(&mut ctx, "receive_ipv6_packet"), 1);
    }

    #[test]
    #[should_panic]
    fn receive_frame_ipv4_uninitialized() {
        test_receive_ip_frame::<Ipv4>(false);
    }

    #[test]
    #[should_panic]
    fn receive_frame_ipv6_uninitialized() {
        test_receive_ip_frame::<Ipv6>(false);
    }

    #[test]
    fn receive_frame_ipv4_initialized() {
        test_receive_ip_frame::<Ipv4>(true);
    }

    #[test]
    fn receive_frame_ipv6_initialized() {
        test_receive_ip_frame::<Ipv6>(true);
    }

    #[specialize_ip]
    fn test_send_ip_frame<I: Ip>(initialize: bool) {
        //
        // Should only send a frame if the device is initialized
        //

        let config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::default().build::<DummyEventDispatcher>();
        let device = ctx.state_mut().add_ethernet_device(config.local_mac, crate::ip::IPV6_MIN_MTU);

        #[ipv4]
        let mut bytes = dns_request_v4::ETHERNET_FRAME.bytes.to_vec();

        #[ipv6]
        let mut bytes = dns_request_v6::ETHERNET_FRAME.bytes.to_vec();

        let mac_bytes = config.local_mac.bytes();
        bytes[6..12].copy_from_slice(&mac_bytes);

        if initialize {
            crate::device::initialize_device(&mut ctx, device);
        }

        // Will panic if we do not initialize.
        crate::device::send_ip_frame(&mut ctx, device, config.remote_ip, Buf::new(bytes, ..));
    }

    #[test]
    #[should_panic]
    fn send_frame_ipv4_uninitialized() {
        test_send_ip_frame::<Ipv4>(false);
    }

    #[test]
    #[should_panic]
    fn send_frame_ipv6_uninitialized() {
        test_send_ip_frame::<Ipv6>(false);
    }

    #[test]
    fn send_frame_ipv4_initialized() {
        test_send_ip_frame::<Ipv4>(true);
    }

    #[test]
    fn send_frame_ipv6_initialized() {
        test_send_ip_frame::<Ipv6>(true);
    }

    #[test]
    fn initialize_once() {
        let mut ctx = DummyEventDispatcherBuilder::default().build::<DummyEventDispatcher>();
        let device =
            ctx.state_mut().add_ethernet_device(DUMMY_CONFIG_V4.local_mac, crate::ip::IPV6_MIN_MTU);
        crate::device::initialize_device(&mut ctx, device);
    }

    #[test]
    #[should_panic]
    fn initialize_multiple() {
        let mut ctx = DummyEventDispatcherBuilder::default().build::<DummyEventDispatcher>();
        let device =
            ctx.state_mut().add_ethernet_device(DUMMY_CONFIG_V4.local_mac, crate::ip::IPV6_MIN_MTU);
        crate::device::initialize_device(&mut ctx, device);

        // Should panic since we are already initialized.
        crate::device::initialize_device(&mut ctx, device);
    }

    fn test_set_ip_routing<I: Ip>()
    where
        I: IcmpIpExt,
        IcmpDestUnreachable: for<'a> IcmpMessage<I, &'a [u8]>,
    {
        #[specialize_ip]
        fn check_other_is_routing_enabled<I: Ip>(
            ctx: &Context<DummyEventDispatcher>,
            device: DeviceId,
            expected: bool,
        ) {
            #[ipv4]
            assert_eq!(is_routing_enabled::<_, Ipv6>(ctx, device), expected);

            #[ipv6]
            assert_eq!(is_routing_enabled::<_, Ipv4>(ctx, device), expected);
        }

        #[specialize_ip]
        fn check_icmp<I: Ip>(buf: &[u8]) {
            #[ipv4]
            let (src_mac, dst_mac, src_ip, dst_ip, message, code) =
                parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, IcmpDestUnreachable, _>(
                    buf,
                    |_| {},
                )
                .unwrap();

            #[ipv6]
            let (src_mac, dst_mac, src_ip, dst_ip, message, code) =
                parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, IcmpDestUnreachable, _>(
                    buf,
                    |_| {},
                )
                .unwrap();
        }

        let src_ip = get_other_ip_address::<I::Addr>(3);
        let src_mac = Mac::new([10, 11, 12, 13, 14, 15]);
        let config = get_dummy_config::<I::Addr>();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;
        let mut rng = new_rng(70812476915813);
        let mut body: Vec<u8> = std::iter::repeat_with(|| rng.gen()).take(100).collect();
        let buf = Buf::new(&mut body[..], ..)
            .encapsulate(<I as IpExt>::PacketBuilder::new(
                src_ip.get(),
                config.remote_ip.get(),
                64,
                IpProto::Tcp,
            ))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .unwrap_b();

        //
        // Test with netstack no fowarding
        //

        let mut builder = DummyEventDispatcherBuilder::from_config(config.clone());
        add_arp_or_ndp_table_entry(&mut builder, device.id(), src_ip.get(), src_mac);
        let mut ctx = builder.build();

        // Should not be a router (default).
        assert!(!is_routing_enabled::<_, I>(&ctx, device));
        check_other_is_routing_enabled::<I>(&ctx, device, false);

        // Receiving a packet not destined for the node should result in a dest unreachable message.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(ctx.dispatcher().frames_sent().len(), 1);
        check_icmp::<I>(&ctx.dispatcher().frames_sent()[0].1);

        // Attempting to set router should work, but it still won't be able to
        // route packets.
        set_routing_enabled::<_, I>(&mut ctx, device, true);
        assert!(is_routing_enabled::<_, I>(&ctx, device));
        // Should not update other Ip routing status.
        check_other_is_routing_enabled::<I>(&ctx, device, false);
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(ctx.dispatcher().frames_sent().len(), 2);
        check_icmp::<I>(&ctx.dispatcher().frames_sent()[1].1);

        //
        // Test with netstack fowarding
        //

        let mut state_builder = StackStateBuilder::default();
        state_builder.ipv4_builder().forward(true);
        state_builder.ipv6_builder().forward(true);
        // Most tests do not need NDP's DAD or router solicitation so disable it here.
        let mut ndp_configs = ndp::NdpConfigurations::default();
        ndp_configs.set_dup_addr_detect_transmits(None);
        ndp_configs.set_max_router_solicitations(None);
        state_builder.device_builder().set_default_ndp_configs(ndp_configs);
        let mut builder = DummyEventDispatcherBuilder::from_config(config.clone());
        add_arp_or_ndp_table_entry(&mut builder, device.id(), src_ip.get(), src_mac);
        let mut ctx = builder.build_with(state_builder, DummyEventDispatcher::default());

        // Should not be a router (default).
        assert!(!is_routing_enabled::<_, I>(&ctx, device));
        check_other_is_routing_enabled::<I>(&ctx, device, false);

        // Receiving a packet not destined for the node should result in a dest unreachable message.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(ctx.dispatcher().frames_sent().len(), 1);
        check_icmp::<I>(&ctx.dispatcher().frames_sent()[0].1);

        // Attempting to set router should work
        set_routing_enabled::<_, I>(&mut ctx, device, true);
        assert!(is_routing_enabled::<_, I>(&ctx, device));
        // Should not update other Ip routing status.
        check_other_is_routing_enabled::<I>(&ctx, device, false);

        // Should route the packet since routing fully enabled (netstack & device).
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(ctx.dispatcher().frames_sent().len(), 2);
        println!("{:?}", buf.as_ref());
        println!("{:?}", ctx.dispatcher().frames_sent()[1].1);
        let (packet_buf, _, _, packet_src_ip, packet_dst_ip, proto) =
            parse_ip_packet_in_ethernet_frame::<I>(&ctx.dispatcher().frames_sent()[1].1[..])
                .unwrap();
        assert_eq!(src_ip.get(), packet_src_ip);
        assert_eq!(config.remote_ip.get(), packet_dst_ip);
        assert_eq!(proto, IpProto::Tcp);
        assert_eq!(body, packet_buf);

        // Attempt to unset router
        set_routing_enabled::<_, I>(&mut ctx, device, false);
        assert!(!is_routing_enabled::<_, I>(&ctx, device));
        check_other_is_routing_enabled::<I>(&ctx, device, false);

        // Should not route packets anymore
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(ctx.dispatcher().frames_sent().len(), 3);
        check_icmp::<I>(&ctx.dispatcher().frames_sent()[2].1);
    }

    #[test]
    fn test_set_ipv4_routing() {
        test_set_ip_routing::<Ipv4>();
    }

    #[test]
    fn test_set_ipv6_routing() {
        test_set_ip_routing::<Ipv6>();
    }

    fn test_promiscuous_mode<I: Ip>() {
        //
        // Test that frames not destined for a device will still be accepted when
        // the device is put into promiscuous mode. In all cases, frames that are
        // destined for a device must always be accepted.
        //

        let config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let other_mac = Mac::new([13, 14, 15, 16, 17, 18]);

        let buf = Buf::new(Vec::new(), ..)
            .encapsulate(<I as IpExt>::PacketBuilder::new(
                config.remote_ip.get(),
                config.local_ip.get(),
                64,
                IpProto::Tcp,
            ))
            .encapsulate(EthernetFrameBuilder::new(
                config.remote_mac,
                config.local_mac,
                I::ETHER_TYPE,
            ))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .unwrap_b();

        // Accept packet destined for this device if promiscuous mode is off.
        crate::device::set_promiscuous_mode(&mut ctx, device, false);
        crate::device::receive_frame(&mut ctx, device, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        // Accept packet destined for this device if promiscuous mode is on.
        crate::device::set_promiscuous_mode(&mut ctx, device, true);
        crate::device::receive_frame(&mut ctx, device, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 2);

        let buf = Buf::new(Vec::new(), ..)
            .encapsulate(<I as IpExt>::PacketBuilder::new(
                config.remote_ip.get(),
                config.local_ip.get(),
                64,
                IpProto::Tcp,
            ))
            .encapsulate(EthernetFrameBuilder::new(config.remote_mac, other_mac, I::ETHER_TYPE))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .unwrap_b();

        // Reject packet not destined for this device if promiscuous mode is off.
        crate::device::set_promiscuous_mode(&mut ctx, device, false);
        crate::device::receive_frame(&mut ctx, device, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 2);

        // Accept packet not destined for this device if promiscuous mode is on.
        crate::device::set_promiscuous_mode(&mut ctx, device, true);
        crate::device::receive_frame(&mut ctx, device, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 3);
    }

    #[test]
    fn test_promiscuous_mode_ipv4() {
        test_promiscuous_mode::<Ipv4>();
    }

    #[test]
    fn test_promiscuous_mode_ipv6() {
        test_promiscuous_mode::<Ipv6>();
    }

    fn test_add_remove_ip_addresses<I: Ip>() {
        let config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::default().build::<DummyEventDispatcher>();
        let device = ctx.state_mut().add_ethernet_device(config.local_mac, crate::ip::IPV6_MIN_MTU);
        crate::device::initialize_device(&mut ctx, device);

        let ip1 = get_other_ip_address::<I::Addr>(1);
        let ip2 = get_other_ip_address::<I::Addr>(2);
        let ip3 = get_other_ip_address::<I::Addr>(3);

        let prefix = I::Addr::BYTES * 8;
        let as1 = AddrSubnet::new(ip1.get(), prefix).unwrap();
        let as2 = AddrSubnet::new(ip2.get(), prefix).unwrap();

        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).is_none());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).is_none());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip3).is_none());

        // Add ip1 (ok)
        crate::device::add_ip_addr_subnet(&mut ctx, device, as1).unwrap();
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).is_some());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).is_none());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip3).is_none());

        // Add ip2 (ok)
        crate::device::add_ip_addr_subnet(&mut ctx, device, as2).unwrap();
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).is_some());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).is_some());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip3).is_none());

        // Del ip1 (ok)
        crate::device::del_ip_addr(&mut ctx, device, &ip1).unwrap();
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).is_none());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).is_some());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip3).is_none());

        // Del ip1 again (ip1 not found)
        assert_eq!(
            crate::device::del_ip_addr(&mut ctx, device, &ip1).unwrap_err(),
            AddressError::NotFound
        );
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).is_none());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).is_some());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip3).is_none());

        // Add ip2 again (ip2 already exists)
        assert_eq!(
            crate::device::add_ip_addr_subnet(&mut ctx, device, as2).unwrap_err(),
            AddressError::AlreadyExists
        );
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).is_none());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).is_some());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip3).is_none());

        // Add ip2 with different subnet (ip2 already exists)
        assert_eq!(
            crate::device::add_ip_addr_subnet(
                &mut ctx,
                device,
                AddrSubnet::new(ip2.get(), prefix - 1).unwrap()
            )
            .unwrap_err(),
            AddressError::AlreadyExists
        );
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).is_none());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).is_some());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip3).is_none());
    }

    #[test]
    fn test_add_remove_ipv4_addresses() {
        test_add_remove_ip_addresses::<Ipv4>();
    }

    #[test]
    fn test_add_remove_ipv6_addresses() {
        test_add_remove_ip_addresses::<Ipv6>();
    }

    fn test_multiple_ip_addresses<I: Ip>() {
        fn inner_test<A: IpAddress>(
            ctx: &mut Context<DummyEventDispatcher>,
            device: DeviceId,
            src_ip: A,
            dst_ip: A,
            expected: usize,
        ) {
            let buf = Buf::new(Vec::new(), ..)
                .encapsulate(<A::Version as IpExt>::PacketBuilder::new(
                    src_ip,
                    dst_ip,
                    64,
                    IpProto::Tcp,
                ))
                .serialize_vec_outer()
                .ok()
                .unwrap()
                .into_inner();

            receive_ip_packet::<_, _, A::Version>(ctx, device, FrameDestination::Unicast, buf);
            assert_eq!(get_counter_val(ctx, dispatch_receive_ip_packet_name::<A::Version>()), expected);
        }

        let config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::default().build::<DummyEventDispatcher>();
        let device = ctx.state_mut().add_ethernet_device(config.local_mac, crate::ip::IPV6_MIN_MTU);
        crate::device::initialize_device(&mut ctx, device);

        let ip1 = get_other_ip_address::<I::Addr>(1);
        let ip2 = get_other_ip_address::<I::Addr>(2);
        let from_ip = get_other_ip_address::<I::Addr>(3).get();

        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).is_none());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).is_none());

        // Should not receive packets on any ip.
        inner_test(&mut ctx, device, from_ip, ip1.get(), 0);
        inner_test(&mut ctx, device, from_ip, ip2.get(), 0);

        // Add ip1 to device.
        crate::device::add_ip_addr_subnet(
            &mut ctx,
            device,
            AddrSubnet::new(ip1.get(), I::Addr::BYTES * 8).unwrap(),
        )
        .unwrap();
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).unwrap().is_assigned());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).is_none());

        // Should receive packets on ip1 but not ip2
        inner_test(&mut ctx, device, from_ip, ip1.get(), 1);
        inner_test(&mut ctx, device, from_ip, ip2.get(), 1);

        // Add ip2 to device.
        crate::device::add_ip_addr_subnet(
            &mut ctx,
            device,
            AddrSubnet::new(ip2.get(), I::Addr::BYTES * 8).unwrap(),
        )
        .unwrap();
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).unwrap().is_assigned());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).unwrap().is_assigned());

        // Should receive packets on both ips
        inner_test(&mut ctx, device, from_ip, ip1.get(), 2);
        inner_test(&mut ctx, device, from_ip, ip2.get(), 3);

        // Remove ip1
        crate::device::del_ip_addr(&mut ctx, device, &ip1).unwrap();
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip1).is_none());
        assert!(crate::device::get_ip_addr_state(ctx.state(), device, &ip2).unwrap().is_assigned());

        // Should receive packets on ip2
        inner_test(&mut ctx, device, from_ip, ip1.get(), 3);
        inner_test(&mut ctx, device, from_ip, ip2.get(), 4);
    }

    #[test]
    fn test_multiple_ipv4_addresses() {
        test_multiple_ip_addresses::<Ipv4>();
    }

    #[test]
    fn test_multiple_ipv6_addresses() {
        test_multiple_ip_addresses::<Ipv6>();
    }
}
