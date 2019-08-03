// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The device layer.

pub(crate) mod arp;
pub(crate) mod ethernet;
pub(crate) mod ndp;

use std::fmt::{self, Debug, Display, Formatter};

use log::{debug, trace};
use net_types::ethernet::Mac;
use net_types::ip::{AddrSubnet, IpAddress, Ipv4Addr, Ipv6, Ipv6Addr};
use net_types::{LinkLocalAddr, MulticastAddr};
use packet::{BufferMut, Serializer};

use crate::data_structures::{IdMap, IdMapCollectionKey};
use crate::device::ethernet::{EthernetDeviceState, EthernetDeviceStateBuilder};
use crate::{BufferDispatcher, Context, EventDispatcher, StackState};

/// An ID identifying a device.
#[derive(Copy, Clone, Eq, PartialEq, Hash)]
pub struct DeviceId {
    id: usize,
    protocol: DeviceProtocol,
}

impl DeviceId {
    /// Construct a new `DeviceId` for an Ethernet device.
    pub(crate) fn new_ethernet(id: usize) -> DeviceId {
        DeviceId { id, protocol: DeviceProtocol::Ethernet }
    }

    /// Get the protocol-specific ID for this `DeviceId`.
    pub fn id(self) -> usize {
        self.id
    }

    /// Get the protocol for this `DeviceId`.
    pub fn protocol(self) -> DeviceProtocol {
        self.protocol
    }
}

impl Display for DeviceId {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(f, "{}:{}", self.protocol, self.id)
    }
}

impl Debug for DeviceId {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        Display::fmt(self, f)
    }
}

impl IdMapCollectionKey for DeviceId {
    const VARIANT_COUNT: usize = 1;

    fn get_variant(&self) -> usize {
        match self.protocol {
            DeviceProtocol::Ethernet => 0,
        }
    }

    fn get_id(&self) -> usize {
        self.id as usize
    }
}

/// Type of device protocol.
#[derive(Copy, Clone, Eq, PartialEq, Hash)]
pub enum DeviceProtocol {
    Ethernet,
}

impl Display for DeviceProtocol {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(
            f,
            "{}",
            match self {
                DeviceProtocol::Ethernet => "Ethernet",
            }
        )
    }
}

// TODO(joshlf): Does the IP layer ever need to distinguish between broadcast
// and multicast frames?

/// The type of address used as the source address in a device-layer frame:
/// unicast or broadcast.
///
/// `FrameDestination` is used to implement RFC 1122 section 3.2.2 and RFC 4443
/// section 2.4.e, which govern when to avoid sending an ICMP error message for
/// ICMP and ICMPv6 respectively.
#[derive(Copy, Clone, Eq, PartialEq)]
pub(crate) enum FrameDestination {
    /// A unicast address - one which is neither multicast nor broadcast.
    Unicast,
    /// A multicast address; if the addressing scheme supports overlap between
    /// multicast and broadcast, then broadcast addresses should use the
    /// `Broadcast` variant.
    Multicast,
    /// A broadcast address; if the addressing scheme supports overlap between
    /// multicast and broadcast, then broadcast addresses should use the
    /// `Broadcast` variant.
    Broadcast,
}

impl FrameDestination {
    /// Is this `FrameDestination::Multicast`?
    pub(crate) fn is_multicast(self) -> bool {
        self == FrameDestination::Multicast
    }

    /// Is this `FrameDestination::Broadcast`?
    pub(crate) fn is_broadcast(self) -> bool {
        self == FrameDestination::Broadcast
    }
}

/// Builder for a [`DeviceLayerState`].
#[derive(Clone)]
pub struct DeviceStateBuilder {
    /// Default values for NDP's configurations for new interfaces.
    ///
    /// See [`ndp::NdpConfigurations`].
    default_ndp_configs: ndp::NdpConfigurations,
}

impl Default for DeviceStateBuilder {
    fn default() -> Self {
        Self { default_ndp_configs: ndp::NdpConfigurations::default() }
    }
}

impl DeviceStateBuilder {
    /// Set the default values for NDP's configurations for new interfaces.
    ///
    /// See [`ndp::NdpConfigurations`] for more details.
    pub fn set_default_ndp_configs(&mut self, v: ndp::NdpConfigurations) {
        self.default_ndp_configs = v;
    }

    /// Build the [`DeviceLayerState`].
    pub(crate) fn build(self) -> DeviceLayerState {
        DeviceLayerState { ethernet: IdMap::new(), default_ndp_configs: self.default_ndp_configs }
    }
}

/// The state associated with the device layer.
pub(crate) struct DeviceLayerState {
    ethernet: IdMap<DeviceState<EthernetDeviceState>>,
    default_ndp_configs: ndp::NdpConfigurations,
}

impl DeviceLayerState {
    /// Add a new ethernet device to the device layer.
    ///
    /// `add` adds a new `EthernetDeviceState` with the given MAC address and
    /// MTU. The MTU will be taken as a limit on the size of Ethernet payloads -
    /// the Ethernet header is not counted towards the MTU.
    pub(crate) fn add_ethernet_device(&mut self, mac: Mac, mtu: u32) -> DeviceId {
        let mut builder = EthernetDeviceStateBuilder::new(mac, mtu);
        builder.set_ndp_configs(self.default_ndp_configs.clone());
        let mut ethernet_state = DeviceState::new(builder.build());
        let id = self.ethernet.push(ethernet_state);
        debug!("adding Ethernet device with ID {} and MTU {}", id, mtu);
        DeviceId::new_ethernet(id)
    }

    // TODO(rheacock, NET-2140): Add ability to remove inactive devices
}

/// Common state across devices.
#[derive(Default)]
pub(crate) struct CommonDeviceState {
    /// Is the device initialized?
    is_initialized: bool,
}

/// Device state.
///
/// `D` is the device-specific state.
pub(crate) struct DeviceState<D> {
    /// Device-independant state.
    common: CommonDeviceState,

    /// Device-specific state.
    device: D,
}

impl<D> DeviceState<D> {
    /// Create a new `DeviceState` with a device-specific state `device`.
    pub(crate) fn new(device: D) -> Self {
        Self { common: CommonDeviceState::default(), device }
    }

    /// Get a reference to the common (device-independant) state.
    pub(crate) fn common(&self) -> &CommonDeviceState {
        &self.common
    }

    /// Get a mutable reference to the common (device-independant) state.
    pub(crate) fn common_mut(&mut self) -> &mut CommonDeviceState {
        &mut self.common
    }

    /// Get a reference to the inner (device-specific) state.
    pub(crate) fn device(&self) -> &D {
        &self.device
    }

    /// Get a mutable reference to the inner (device-specific) state.
    pub(crate) fn device_mut(&mut self) -> &mut D {
        &mut self.device
    }
}

/// The identifier for timer events in the device layer.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) enum DeviceLayerTimerId {
    /// A timer event in the ARP layer with a protocol type of IPv4
    ArpIpv4(arp::ArpTimerId<usize, Ipv4Addr>),
    Ndp(ndp::NdpTimerId),
}

impl From<arp::ArpTimerId<usize, Ipv4Addr>> for DeviceLayerTimerId {
    fn from(id: arp::ArpTimerId<usize, Ipv4Addr>) -> DeviceLayerTimerId {
        DeviceLayerTimerId::ArpIpv4(id)
    }
}

/// Handle a timer event firing in the device layer.
pub(crate) fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: DeviceLayerTimerId) {
    match id {
        DeviceLayerTimerId::ArpIpv4(inner_id) => arp::handle_timer(ctx, inner_id),
        DeviceLayerTimerId::Ndp(inner_id) => ndp::handle_timeout(ctx, inner_id),
    }
}

/// An event dispatcher for the device layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait DeviceLayerEventDispatcher<B: BufferMut> {
    /// Send a frame to a device driver.
    ///
    /// If there was an MTU error while attempting to serialize the frame, the
    /// original serializer is returned in the `Err` variant. All other errors
    /// (for example, errors in allocating a buffer) are silently ignored and
    /// reported as success.
    ///
    /// Note, until `device` has been initialized, the netstack promises to not
    /// send any outbound traffic to it. See [`initialize_device`] for more
    /// information.
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        frame: S,
    ) -> Result<(), S>;
}

/// Is `device` initialized?
pub(crate) fn is_device_initialized<D: EventDispatcher>(
    state: &StackState<D>,
    device: DeviceId,
) -> bool {
    get_common_device_state(state, device).is_initialized
}

/// Initialize a device.
///
/// `initialize_device` will start soliciting IPv6 routers on the link if `device` is configured to
/// be a host.
///
/// `initialize_device` MUST be called after adding the device to the netstack. A device MUST NOT
/// be used until it has been initialized.
///
/// This initialize step is kept separated from the device creation/allocation step so that
/// implementations have a chance to do some work (such as updating implementation specific IDs or
/// state, configure the device or driver, etc.) before the device is actually initialized and used
/// by this netstack.
///
/// See [`StackState::add_ethernet_device`] for information about adding ethernet devices.
///
/// # Panics
///
/// Panics if `device` is already initialized.
pub fn initialize_device<D: EventDispatcher>(ctx: &mut Context<D>, device: DeviceId) {
    let state = get_common_device_state_mut(ctx.state_mut(), device);

    // `device` must not already be initialized.
    assert!(!state.is_initialized);

    state.is_initialized = true;

    // RFC 4861 section 6.3.7, it implies only a host sends router
    // solicitation messages, so if this node is a router, do nothing.
    if crate::ip::is_router::<_, Ipv6>(ctx) {
        trace!("intialize_device: node is a router so not starting router solicitations");
        return;
    }

    match device.protocol {
        DeviceProtocol::Ethernet => {
            ndp::start_soliciting_routers::<_, ethernet::EthernetNdpDevice>(ctx, device.id)
        }
    }
}

/// Send an IP packet in a device layer frame.
///
/// `send_ip_frame` accepts a device ID, a local IP address, and a
/// `SerializationRequest`. It computes the routing information and serializes
/// the request in a new device layer frame and sends it.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub(crate) fn send_ip_frame<B: BufferMut, D: BufferDispatcher<B>, A, S>(
    ctx: &mut Context<D>,
    device: DeviceId,
    local_addr: A,
    body: S,
) -> Result<(), S>
where
    A: IpAddress,
    S: Serializer<Buffer = B>,
{
    // `device` must be initialized.
    assert!(is_device_initialized(ctx.state(), device));

    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::send_ip_frame(ctx, device.id, local_addr, body),
    }
}

/// Receive a device layer frame from the network.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub fn receive_frame<B: BufferMut, D: BufferDispatcher<B>>(
    ctx: &mut Context<D>,
    device: DeviceId,
    buffer: B,
) {
    // `device` must be initialized.
    assert!(is_device_initialized(ctx.state(), device));

    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::receive_frame(ctx, device.id, buffer),
    }
}

/// Get the IP address and subnet associated with this device.
///
/// Note, tentative IP addresses (addresses which are not yet fully bound to a
/// device) will not returned by `get_ip_addr_subnet`.
pub fn get_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
    device: DeviceId,
) -> Option<AddrSubnet<A>> {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ip_addr_subnet(ctx, device.id),
    }
}

/// Get the IP address and subnet associated with this device, including tentative
/// address.
pub fn get_ip_addr_subnet_with_tentative<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
    device: DeviceId,
) -> Option<Tentative<AddrSubnet<A>>> {
    match device.protocol {
        DeviceProtocol::Ethernet => {
            self::ethernet::get_ip_addr_subnet_with_tentative(ctx, device.id)
        }
    }
}

/// Set the IP address and subnet associated with this device.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub fn set_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device: DeviceId,
    addr_sub: AddrSubnet<A>,
) {
    // `device` must be initialized.
    assert!(is_device_initialized(ctx.state(), device));

    trace!("set_ip_addr_subnet: setting addr {:?} for device {:?}", addr_sub, device);

    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::set_ip_addr_subnet(ctx, device.id, addr_sub),
    }
}

/// Add `device` to a multicast group `multicast_addr`.
///
/// If `device` is already in the multicast group `multicast_addr`,
/// `join_ip_multicast` does nothing.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub(crate) fn join_ip_multicast<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    // `device` must be initialized.
    assert!(is_device_initialized(ctx.state(), device));

    trace!("join_ip_multicast: device {:?} joining multicast {:?}", device, multicast_addr);

    match device.protocol {
        DeviceProtocol::Ethernet => {
            self::ethernet::join_ip_multicast(ctx, device.id, multicast_addr)
        }
    }
}

/// Remove `device` from a multicast group `multicast_addr`.
///
/// If `device` is not in the multicast group `multicast_addr`,
/// `leave_ip_multicast` does nothing.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub(crate) fn leave_ip_multicast<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    // `device` must be initialized.
    assert!(is_device_initialized(ctx.state(), device));

    trace!("join_ip_multicast: device {:?} leaving multicast {:?}", device, multicast_addr);

    match device.protocol {
        DeviceProtocol::Ethernet => {
            self::ethernet::leave_ip_multicast(ctx, device.id, multicast_addr)
        }
    }
}

/// Is `device` part of the IP multicast group `multicast_addr`.
pub(crate) fn is_in_ip_multicast<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
    device: DeviceId,
    multicast_addr: MulticastAddr<A>,
) -> bool {
    match device.protocol {
        DeviceProtocol::Ethernet => {
            self::ethernet::is_in_ip_multicast(ctx, device.id, multicast_addr)
        }
    }
}

/// Get the MTU associated with this device.
pub(crate) fn get_mtu<D: EventDispatcher>(state: &StackState<D>, device: DeviceId) -> u32 {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_mtu(state, device.id),
    }
}

/// Gets the IPv6 link-local address associated with this device.
// TODO(brunodalbo) when our device model allows for multiple IPs we can have
// a single function go get all the IP addresses associated with a device, which
// would be cleaner and remove the need for this function.
pub fn get_ipv6_link_local_addr<D: EventDispatcher>(
    ctx: &Context<D>,
    device: DeviceId,
) -> LinkLocalAddr<Ipv6Addr> {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ipv6_link_local_addr(ctx, device.id),
    }
}

/// Determine if an IP Address is considered tentative on a device.
///
/// Returns `true` if the address is tentative on a device; `false` otherwise.
/// Note, if the `addr` is not assigned to `device` but is considered tentative
/// on another device, `is_addr_tentative_on_device` will return `false`.
pub(crate) fn is_addr_tentative_on_device<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
    addr: A,
    device: DeviceId,
) -> bool {
    get_ip_addr_subnet_with_tentative::<_, A>(ctx, device)
        .map(|x| (x.inner().addr() == addr) && x.is_tentative())
        .unwrap_or(false)
}

/// Get a reference to the common device state for a `device`.
fn get_common_device_state<D: EventDispatcher>(
    state: &StackState<D>,
    device: DeviceId,
) -> &CommonDeviceState {
    match device.protocol {
        DeviceProtocol::Ethernet => state
            .device
            .ethernet
            .get(device.id)
            .unwrap_or_else(|| panic!("no such Ethernet device: {}", device.id))
            .common(),
    }
}

/// Get a mutable reference to the common device state for a `device`.
fn get_common_device_state_mut<D: EventDispatcher>(
    state: &mut StackState<D>,
    device: DeviceId,
) -> &mut CommonDeviceState {
    match device.protocol {
        DeviceProtocol::Ethernet => state
            .device
            .ethernet
            .get_mut(device.id)
            .unwrap_or_else(|| panic!("no such Ethernet device: {}", device.id))
            .common_mut(),
    }
}

/// An address that may be "tentative" in that it has not yet passed
/// duplicate address detection (DAD).
///
/// A tentative address is one for which DAD is currently being performed.
/// An address is only considered assigned to an interface once DAD has
/// completed without detecting any duplicates. See [RFC 4862] for more details.
///
/// [RFC 4862]: https://tools.ietf.org/html/rfc4862
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Tentative<T>(T, bool);

impl<T> Tentative<T> {
    /// Create a new address that is marked as tentative.
    pub(crate) fn new_tentative(t: T) -> Self {
        Self(t, true)
    }

    /// Create a new address that is marked as permanent/assigned.
    pub(crate) fn new_permanent(t: T) -> Self {
        Self(t, false)
    }

    /// Returns whether the value is tentative.
    pub(crate) fn is_tentative(&self) -> bool {
        self.1
    }

    /// Gets the value that is stored inside.
    pub(crate) fn into_inner(self) -> T {
        self.0
    }

    /// Converts a `Tentative<T>` into a `Option<T>` in the way that
    /// a tentative value corresponds to a `None`.
    pub(crate) fn try_into_permanent(self) -> Option<T> {
        if self.is_tentative() {
            None
        } else {
            Some(self.into_inner())
        }
    }

    /// Borrow the content which is stored inside.
    pub(crate) fn inner(&self) -> &T {
        &self.0
    }

    /// Similar to `Option::map`.
    pub(crate) fn map<U, F>(self, f: F) -> Tentative<U>
    where
        F: FnOnce(T) -> U,
    {
        Tentative(f(self.0), self.1)
    }

    /// Make the tentative value to be permanent.
    pub(crate) fn mark_permanent(&mut self) {
        self.1 = false
    }
}
