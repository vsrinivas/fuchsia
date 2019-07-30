// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The device layer.

pub(crate) mod arp;
pub(crate) mod ethernet;
pub(crate) mod ndp;

use std::fmt::{self, Debug, Display, Formatter};
use std::num::NonZeroU8;

use log::{debug, trace};
use net_types::ethernet::Mac;
use net_types::ip::{AddrSubnet, Ip, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use net_types::{LinkLocalAddr, MulticastAddr, SpecifiedAddr};
use packet::{BufferMut, Serializer};
use specialize_ip_macro::specialize_ip;

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
    pub(crate) fn build<D: EventDispatcher>(self) -> DeviceLayerState<D> {
        DeviceLayerState { ethernet: IdMap::new(), default_ndp_configs: self.default_ndp_configs }
    }
}

/// The state associated with the device layer.
pub(crate) struct DeviceLayerState<D: EventDispatcher> {
    ethernet: IdMap<DeviceState<EthernetDeviceState<D>>>,
    default_ndp_configs: ndp::NdpConfigurations,
}

impl<D: EventDispatcher> DeviceLayerState<D> {
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

/// The various states an IP address can be on an interface.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AddressState {
    /// The address is assigned to an interface and can be considered
    /// bound to it (all packets destined to the address will be
    /// accepted).
    Assigned,

    /// The address is considered unassigned to an interface for normal
    /// operations, but has the intention of being assigned in the future
    /// (e.g. once NDP's Duplicate Address Detection is completed).
    Tentative,
}

impl AddressState {
    /// Is this address assigned?
    pub(crate) fn is_assigned(self) -> bool {
        self == AddressState::Assigned
    }

    /// Is this address tentative?
    pub(crate) fn is_tentative(self) -> bool {
        self == AddressState::Tentative
    }
}

/// Data associated with an IP addressess on an interface.
pub struct AddressEntry<A: IpAddress> {
    addr_sub: AddrSubnet<A>,
    state: AddressState,
}

impl<A: IpAddress> AddressEntry<A> {
    pub(crate) fn new(addr_sub: AddrSubnet<A>, state: AddressState) -> Self {
        Self { addr_sub, state }
    }

    pub(crate) fn addr_sub(&self) -> &AddrSubnet<A> {
        &self.addr_sub
    }

    pub(crate) fn state(&self) -> AddressState {
        self.state
    }

    pub(crate) fn mark_permanent(&mut self) {
        self.state = AddressState::Assigned;
    }
}

/// Possible return values during an erroneous interface address change operation.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum AddressError {
    AlreadyExists,
    NotFound,
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
/// be a host. If it is configured to be an advertising interface, it will start sending periodic
/// router advertisements.
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

    // All nodes should join the all-nodes multicast group.
    join_ip_multicast(ctx, device, MulticastAddr::new(Ipv6::ALL_NODES_LINK_LOCAL_ADDRESS).unwrap());

    if self::is_router_device::<_, Ipv6>(ctx, device) {
        // If the device is operating as a router, and it is configured to be an advertising
        // interface, start sending periodic router advertisements.
        if get_ndp_configurations(ctx, device)
            .get_router_configurations()
            .get_should_send_advertisements()
        {
            match device.protocol {
                DeviceProtocol::Ethernet => ndp::start_advertising_interface::<
                    _,
                    ethernet::EthernetNdpDevice,
                >(ctx, device.id),
            }
        }
    } else {
        // RFC 4861 section 6.3.7, it implies only a host sends router solicitation messages.
        match device.protocol {
            DeviceProtocol::Ethernet => {
                ndp::start_soliciting_routers::<_, ethernet::EthernetNdpDevice>(ctx, device.id)
            }
        }
    }
}

/// Remove a device from the device layer.
///
/// This function returns frames for the bindings to send if the shutdown is graceful - they can be
/// safely ignored otherwise.
///
/// # Panics
///
/// Panics if `device` does not refer to an existing device.
pub fn remove_device<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
) -> Option<Vec<usize>> {
    match device.protocol {
        DeviceProtocol::Ethernet => {
            // TODO(rheacock): Generate any final frames to send here.
            crate::device::ethernet::deinitialize(ctx, device.id);
            ctx.state_mut()
                .device
                .ethernet
                .remove(device.id)
                .unwrap_or_else(|| panic!("no such Ethernet device: {}", device.id));
            debug!("removing Ethernet device with ID {}", device.id);
            None
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
    local_addr: SpecifiedAddr<A>,
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

/// Set the promiscuous mode flag on `device`.
pub(crate) fn set_promiscuous_mode<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    enabled: bool,
) {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::set_promiscuous_mode(ctx, device.id, enabled),
    }
}

/// Get a single IP address and subnet for a device.
///
/// Note, tentative IP addresses (addresses which are not yet fully bound to a
/// device) will not be returned by `get_ip_addr`.
pub(crate) fn get_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    state: &StackState<D>,
    device: DeviceId,
) -> Option<AddrSubnet<A>> {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ip_addr_subnet(state, device.id),
    }
}

/// Get the IP addresses and associated subnets for a device.
///
/// Note, tentative IP addresses (addresses which are not yet fully bound to a
/// device) will not be returned by `get_ip_addr_subnets`.
///
/// Returns an [`Iterator`] of `AddrSubnet<A>`.
///
/// See [`Tentative`] and [`AddrSubnet`] for more information.
pub fn get_ip_addr_subnets<'a, D: EventDispatcher, A: IpAddress>(
    state: &'a StackState<D>,
    device: DeviceId,
) -> impl 'a + Iterator<Item = AddrSubnet<A>> {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ip_addr_subnets(state, device.id),
    }
}

/// Get the IP addresses and associated subnets for a device, including tentative
/// address.
///
/// Returns an [`Iterator`] of `Tentative<AddrSubnet<A>>`.
///
/// See [`Tentative`] and [`AddrSubnet`] for more information.
pub fn get_ip_addr_subnets_with_tentative<'a, D: EventDispatcher, A: IpAddress>(
    state: &'a StackState<D>,
    device: DeviceId,
) -> impl 'a + Iterator<Item = Tentative<AddrSubnet<A>>> {
    match device.protocol {
        DeviceProtocol::Ethernet => {
            self::ethernet::get_ip_addr_subnets_with_tentative(state, device.id)
        }
    }
}

/// Get the state of an address on device.
///
/// Returns `None` if `addr` is not associated with `device`.
pub fn get_ip_addr_state<D: EventDispatcher, A: IpAddress>(
    state: &StackState<D>,
    device: DeviceId,
    addr: &SpecifiedAddr<A>,
) -> Option<AddressState> {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ip_addr_state(state, device.id, addr),
    }
}

/// Adds an IP address and associated subnet to this device.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub fn add_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device: DeviceId,
    addr_sub: AddrSubnet<A>,
) -> Result<(), AddressError> {
    // `device` must be initialized.
    assert!(is_device_initialized(ctx.state(), device));

    trace!("add_ip_addr_subnet: adding addr {:?} to device {:?}", addr_sub, device);

    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::add_ip_addr_subnet(ctx, device.id, addr_sub),
    }
}

/// Removes an IP address and associated subnet to this device.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub fn del_ip_addr<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device: DeviceId,
    addr: &SpecifiedAddr<A>,
) -> Result<(), AddressError> {
    // `device` must be initialized.
    assert!(is_device_initialized(ctx.state(), device));

    trace!("del_ip_addr: removing addr {:?} from device {:?}", addr, device);

    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::del_ip_addr(ctx, device.id, addr),
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

/// Get the hop limit for new IPv6 packets that will be sent out from `device`.
pub(crate) fn get_ipv6_hop_limit<D: EventDispatcher>(
    ctx: &Context<D>,
    device: DeviceId,
) -> NonZeroU8 {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ipv6_hop_limit(ctx, device.id),
    }
}

/// Gets the IPv6 link-local address associated with this device.
// TODO(brunodalbo) when our device model allows for multiple IPs we can have
// a single function go get all the IP addresses associated with a device, which
// would be cleaner and remove the need for this function.
pub fn get_ipv6_link_local_addr<D: EventDispatcher>(
    ctx: &Context<D>,
    device: DeviceId,
) -> Option<LinkLocalAddr<Ipv6Addr>> {
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
    state: &StackState<D>,
    addr: SpecifiedAddr<A>,
    device: DeviceId,
) -> bool {
    get_ip_addr_subnets_with_tentative::<_, A>(state, device)
        .any(|x| (x.inner().addr() == addr) && x.is_tentative())
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

/// Is IP packet routing enabled on `device`?
///
/// Note, `true` does not necessarily mean that `device` is currently routing IP packets. It
/// only means that `device` is allowed to route packets. To route packets, this netstack must
/// be configured to allow IP packets to be routed if it was not destined for this node.
pub(crate) fn is_routing_enabled<D: EventDispatcher, I: Ip>(
    ctx: &Context<D>,
    device: DeviceId,
) -> bool {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::is_routing_enabled::<_, I>(ctx, device.id),
    }
}

/// Enables or disables IP packet routing on `device`.
///
/// `set_routing_enabled` does nothing if the new routing status, `enabled`, is the same as
/// the current routing status.
///
/// Note, enabling routing does not mean that `device` will immediately start routing IP
/// packets. It only means that `device` is allowed to route packets. To route packets, this
/// netstack must be configured to allow IP packets to be routed if it was not destined for this
/// node.
#[specialize_ip]
pub(crate) fn set_routing_enabled<D: EventDispatcher, I: Ip>(
    ctx: &mut Context<D>,
    device: DeviceId,
    enabled: bool,
) {
    // TODO(ghanan): We cannot directly do `I::VERSION` in the `trace!` calls because of a bug in
    //               specialize_ip_macro where it does not properly replace `I` with `Self`. Once
    //               this is fixed, change this.
    let version = I::VERSION;

    if crate::device::is_routing_enabled::<_, I>(ctx, device) == enabled {
        trace!(
            "set_routing_enabled: {:?} routing status unchanged for device {:?}",
            version,
            device
        );
        return;
    }

    #[ipv4]
    set_ipv4_routing_enabled(ctx, device, enabled);

    #[ipv6]
    set_ipv6_routing_enabled(ctx, device, enabled);
}

/// Sets IPv4 routing on `device`.
fn set_ipv4_routing_enabled<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    enabled: bool,
) {
    if enabled {
        trace!("set_ipv4_routing_enabled: enabling IPv4 routing for device {:?}", device);
    } else {
        trace!("set_ipv4_routing_enabled: disabling IPv4 routing for device {:?}", device);
    }

    set_routing_enabled_inner::<_, Ipv4>(ctx, device, enabled);
}

/// Sets IPv6 routing on `device`.
///
/// If the `device` transitions from a router -> host or host -> router, periodic router
/// advertisements will be stopped or started, and router solicitations will be started or stopped,
/// depending on `device`'s current and new router state.
fn set_ipv6_routing_enabled<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    enabled: bool,
) {
    let ip_routing = crate::ip::is_routing_enabled::<_, Ipv6>(ctx);

    if enabled {
        trace!("set_ipv6_routing_enabled: enabling IPv6 routing for device {:?}", device);

        // Make sure that the netstack is configured to route packets before considering this
        // device a router and stopping router solicitations. If the netstack was not configured
        // to route packets before, then we would still be considered a host, so we shouldn't
        // stop soliciting routers.
        if ip_routing {
            // TODO(ghanan): Handle transition from disabled to enabled:
            //               - start periodic router advertisements (if configured to do so)

            match device.protocol {
                DeviceProtocol::Ethernet => {
                    ndp::stop_soliciting_routers::<_, ethernet::EthernetNdpDevice>(ctx, device.id)
                }
            }
        }

        // Actually update the routing flag.
        set_routing_enabled_inner::<_, Ipv6>(ctx, device, true);

        // Make sure that the netstack is configured to route packets before considering this
        // device a router and starting periodic router advertisements.
        if ip_routing {
            // Now that `device` is a router, join the all-routers multicast group.
            join_ip_multicast(
                ctx,
                device,
                MulticastAddr::new(Ipv6::ALL_ROUTERS_LINK_LOCAL_ADDRESS).unwrap(),
            );

            if get_ndp_configurations(ctx, device)
                .get_router_configurations()
                .get_should_send_advertisements()
            {
                match device.protocol {
                    DeviceProtocol::Ethernet => ndp::start_advertising_interface::<
                        _,
                        ethernet::EthernetNdpDevice,
                    >(ctx, device.id),
                }
            }
        }
    } else {
        trace!("set_ipv6_routing_enabled: disabling IPv6 routing for device {:?}", device);

        // Make sure that the netstack is configured to route packets before considering this
        // device a router and stopping periodic router advertisements. If the netstack was not
        // configured to route packets before, then we would still be considered a host, so we
        // wouldn't have any periodic router advertisements to stop.
        if ip_routing {
            // Make sure that the device was configured to send advertisements before stopping it.
            // If it was never configured to stop advertisements, there should be nothing to stop.
            if get_ndp_configurations(ctx, device)
                .get_router_configurations()
                .get_should_send_advertisements()
            {
                match device.protocol {
                    DeviceProtocol::Ethernet => ndp::stop_advertising_interface::<
                        _,
                        ethernet::EthernetNdpDevice,
                    >(ctx, device.id),
                }
            }

            // Now that `device` is a host, leave the all-routers multicast group.
            leave_ip_multicast(
                ctx,
                device,
                MulticastAddr::new(Ipv6::ALL_ROUTERS_LINK_LOCAL_ADDRESS).unwrap(),
            );
        }

        // Actually update the routing flag.
        set_routing_enabled_inner::<_, Ipv6>(ctx, device, false);

        // We only need to start soliciting routers if we were not soliciting them before. We
        // would only reach this point if there was a change in routing status for `device`.
        // However, if the nestatck does not currently have routing enabled, the device would
        // not have been considered a router before this routing change on the device, so it
        // would have already solicited routers.
        if ip_routing {
            // On transition from router -> host, start soliciting router information.
            match device.protocol {
                DeviceProtocol::Ethernet => {
                    ndp::start_soliciting_routers::<_, ethernet::EthernetNdpDevice>(ctx, device.id)
                }
            }
        }
    }
}

/// Sets the IP packet routing flag on `device`.
fn set_routing_enabled_inner<D: EventDispatcher, I: Ip>(
    ctx: &mut Context<D>,
    device: DeviceId,
    enabled: bool,
) {
    match device.protocol {
        DeviceProtocol::Ethernet => {
            self::ethernet::set_routing_enabled_inner::<_, I>(ctx, device.id, enabled)
        }
    }
}

/// Is `device` currently operating as a router?
///
/// Returns `true` if both the `device` has routing enabled AND the netstack is configured to
/// route packets not destined for it; returns `false` otherwise.
pub(crate) fn is_router_device<D: EventDispatcher, I: Ip>(
    ctx: &Context<D>,
    device: DeviceId,
) -> bool {
    (crate::ip::is_routing_enabled::<_, I>(ctx)
        && crate::device::is_routing_enabled::<_, I>(ctx, device))
}

/// Updates the NDP Configurations for a `device`.
///
/// Note, some values may not take effect immediately, and may only take effect the next time they
/// are used. These scenarios documented below:
///
///  - Updates to [`NdpConfiguration::dup_addr_detect_transmits`] will only take effect the next
///    time Duplicate Address Detection (DAD) is done. Any DAD processes that have already started
///    will continue using the old value.
///
///  - Updates to [`NdpConfiguration::max_router_solicitations`] will only take effect the next
///    time routers are explicitly solicited. Current router solicitation will continue using the
///    old value.
pub fn set_ndp_configurations<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    configs: ndp::NdpConfigurations,
) {
    match device.protocol {
        DeviceProtocol::Ethernet => {
            ndp::set_ndp_configurations::<_, ethernet::EthernetNdpDevice>(ctx, device.id, configs)
        }
    }
}

/// Gets the NDP Configurations for a `device`.
pub fn get_ndp_configurations<D: EventDispatcher>(
    ctx: &Context<D>,
    device: DeviceId,
) -> &ndp::NdpConfigurations {
    match device.protocol {
        DeviceProtocol::Ethernet => {
            ndp::get_ndp_configurations::<_, ethernet::EthernetNdpDevice>(ctx, device.id)
        }
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
