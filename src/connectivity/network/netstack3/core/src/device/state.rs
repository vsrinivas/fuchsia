// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! State maintained by the device layer.

use alloc::vec::Vec;
use core::{fmt::Debug, num::NonZeroU8};

use net_types::{
    ip::{AddrSubnet, Ipv4, Ipv6, Ipv6Addr, Ipv6Scope},
    ScopeableAddress as _, UnicastAddr, Witness,
};

use crate::{
    device::{AssignedAddress as _, DeviceIpExt},
    ip::gmp::MulticastGroupSet,
    Instant,
};

/// Initialization status of a device.
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum InitializationStatus {
    /// The device is not yet initialized and MUST NOT be used.
    Uninitialized,

    /// The device is currently being initialized and must only be used by
    /// the initialization methods.
    Initializing,

    /// The device is initialized and can operate as normal.
    Initialized,
}

impl Default for InitializationStatus {
    fn default() -> InitializationStatus {
        InitializationStatus::Uninitialized
    }
}

/// Common state across devices.
#[derive(Default)]
pub(crate) struct CommonDeviceState {
    /// The device's initialization status.
    initialization_status: InitializationStatus,
}

impl CommonDeviceState {
    pub(crate) fn is_initialized(&self) -> bool {
        self.initialization_status == InitializationStatus::Initialized
    }

    pub(crate) fn is_uninitialized(&self) -> bool {
        self.initialization_status == InitializationStatus::Uninitialized
    }

    pub(crate) fn set_initialization_status(&mut self, status: InitializationStatus) {
        self.initialization_status = status;
    }
}

/// Device state.
///
/// `D` is the device-specific state.
pub(crate) struct DeviceState<D> {
    /// Device-independant state.
    pub(crate) common: CommonDeviceState,

    /// Device-specific state.
    pub(crate) device: D,
}

impl<D> DeviceState<D> {
    /// Creates a new `DeviceState` with a device-specific state `device`.
    pub(crate) fn new(device: D) -> Self {
        Self { common: CommonDeviceState::default(), device }
    }
}

/// The state common to all IP devices.
pub(crate) struct IpDeviceState<Instant, I: DeviceIpExt<Instant>> {
    /// IP addresses assigned to this device.
    ///
    /// IPv6 addresses may be tentative (performing NDP's Duplicate Address
    /// Detection).
    addrs: Vec<I::AssignedAddress>,

    /// Multicast groups this device has joined.
    pub multicast_groups: MulticastGroupSet<I::Addr, I::GmpState>,

    /// Is a Group Messaging Protocol (GMP) enabled for this device?
    ///
    /// If `gmp_enabled` is false, multicast groups will still be added to
    /// `multicast_groups`, but we will not inform the network of our membership
    /// in those groups using a GMP.
    ///
    /// Default: `false`.
    gmp_enabled: bool,

    /// The default TTL (IPv4) or hop limit (IPv6) for outbound packets sent
    /// over this device.
    pub default_hop_limit: NonZeroU8,

    // TODO(https://fxbug.dev/85682): Rename this flag to something like
    // `forwarding_enabled`, and make it control only forwarding. Make
    // participation in Router NDP a separate flag owned by the `ndp` module.
    //
    // TODO(joshlf): The `routing_enabled` field probably doesn't make sense for
    // loopback devices.
    /// A flag indicating whether routing of IP packets not destined for this
    /// device is enabled.
    ///
    /// This flag controls whether or not packets can be routed from this
    /// device. That is, when a packet arrives at a device it is not destined
    /// for, the packet can only be routed if the device it arrived at has
    /// routing enabled and there exists another device that has a path to the
    /// packet's destination, regardless of the other device's routing ability.
    ///
    /// Default: `false`.
    pub routing_enabled: bool,
}

impl<Instant, I: DeviceIpExt<Instant>> Default for IpDeviceState<Instant, I> {
    fn default() -> IpDeviceState<Instant, I> {
        IpDeviceState {
            addrs: Vec::default(),
            multicast_groups: MulticastGroupSet::default(),
            gmp_enabled: false,
            default_hop_limit: I::DEFAULT_HOP_LIMIT,
            routing_enabled: false,
        }
    }
}

// TODO(https://fxbug.dev/84871): Once we figure out what invariants we want to
// hold regarding the set of IP addresses assigned to a device, ensure that all
// of the methods on `IpDeviceState` uphold those invariants.
impl<Instant, I: DeviceIpExt<Instant>> IpDeviceState<Instant, I> {
    /// Iterates over the addresses assigned to this device.
    pub(crate) fn iter_addrs(
        &self,
    ) -> impl ExactSizeIterator<Item = &I::AssignedAddress> + ExactSizeIterator + Clone {
        self.addrs.iter()
    }

    /// Iterates mutably over the addresses assigned to this device.
    pub(crate) fn iter_addrs_mut(
        &mut self,
    ) -> impl ExactSizeIterator<Item = &mut I::AssignedAddress> + ExactSizeIterator {
        self.addrs.iter_mut()
    }

    /// Finds the entry for `addr` if any.
    pub(crate) fn find_addr(&self, addr: &I::Addr) -> Option<&I::AssignedAddress> {
        self.addrs.iter().find(|entry| &entry.addr().get() == addr)
    }

    /// Adds an IP address to this interface.
    pub(crate) fn add_addr(&mut self, addr: I::AssignedAddress) {
        self.addrs.push(addr);
    }

    /// Retains only the assigned addresses specifies by the predicate.
    pub(crate) fn retain_addrs<F: FnMut(&I::AssignedAddress) -> bool>(&mut self, f: F) {
        self.addrs.retain(f);
    }

    /// Is a Group Messaging Protocol (GMP) enabled for this device?
    ///
    /// If a GMP is not enabled, multicast groups will still be added to
    /// `multicast_groups`, but we will not inform the network of our membership
    /// in those groups using a GMP.
    pub(crate) fn gmp_enabled(&self) -> bool {
        self.gmp_enabled
    }
}

/// The state common to all IPv4 devices.
pub(crate) struct Ipv4DeviceState<I: Instant> {
    pub(crate) ip_state: IpDeviceState<I, Ipv4>,
}

impl<I: Instant> Default for Ipv4DeviceState<I> {
    fn default() -> Ipv4DeviceState<I> {
        Ipv4DeviceState { ip_state: Default::default() }
    }
}

/// Configuration common to all IPv6 devices.
#[derive(Clone)]
pub struct Ipv6DeviceConfiguration {
    /// The value for NDP's DupAddrDetectTransmits parameter as defined by
    /// [RFC 4862 section 5.1].
    ///
    /// A value of `None` means DAD will not be performed on the interface.
    ///
    /// [RFC 4862 section 5.1]: https://datatracker.ietf.org/doc/html/rfc4862#section-5.1
    pub(crate) dad_transmits: Option<NonZeroU8>,
}

impl Ipv6DeviceConfiguration {
    /// Sets the value for NDP's DupAddrDetectTransmits parameter as defined by
    /// [RFC 4862 section 5.1].
    ///
    /// A value of `None` means DAD will not be performed on the interface.
    ///
    /// [RFC 4862 section 5.1]: https://datatracker.ietf.org/doc/html/rfc4862#section-5.1
    pub fn set_dad_transmits(&mut self, v: Option<NonZeroU8>) {
        self.dad_transmits = v;
    }
}

impl Default for Ipv6DeviceConfiguration {
    fn default() -> Ipv6DeviceConfiguration {
        Ipv6DeviceConfiguration {
            dad_transmits: NonZeroU8::new(super::ndp::DUP_ADDR_DETECT_TRANSMITS),
        }
    }
}

/// The state common to all IPv6 devices.
pub(crate) struct Ipv6DeviceState<I: Instant> {
    pub(crate) ip_state: IpDeviceState<I, Ipv6>,
    pub(crate) config: Ipv6DeviceConfiguration,
}

impl<I: Instant> Default for Ipv6DeviceState<I> {
    fn default() -> Ipv6DeviceState<I> {
        Ipv6DeviceState { ip_state: Default::default(), config: Default::default() }
    }
}

impl<I: Instant> IpDeviceState<I, Ipv6> {
    /// Iterates over the global IPv6 address entries.
    pub(crate) fn iter_global_ipv6_addrs(
        &self,
    ) -> impl Iterator<Item = &Ipv6AddressEntry<I>> + Clone {
        self.addrs.iter().filter(|entry| entry.is_global())
    }

    /// Iterates mutably over the global IPv6 address entries.
    pub(crate) fn iter_global_ipv6_addrs_mut(
        &mut self,
    ) -> impl Iterator<Item = &mut Ipv6AddressEntry<I>> {
        self.addrs.iter_mut().filter(|entry| entry.is_global())
    }
}

/// IPv4 and IPv6 state combined.
pub(crate) struct DualStackIpDeviceState<I: Instant> {
    /// IPv4 state.
    pub ipv4: Ipv4DeviceState<I>,

    /// IPv6 state.
    pub ipv6: Ipv6DeviceState<I>,
}

impl<I: Instant> Default for DualStackIpDeviceState<I> {
    fn default() -> DualStackIpDeviceState<I> {
        DualStackIpDeviceState {
            ipv4: Ipv4DeviceState::default(),
            ipv6: Ipv6DeviceState::default(),
        }
    }
}

/// State for a link-device that is also an IP device.
///
/// `D` is the link-specific state.
pub(crate) struct IpLinkDeviceState<I: Instant, D> {
    pub ip: DualStackIpDeviceState<I>,
    pub link: D,
}

impl<I: Instant, D> IpLinkDeviceState<I, D> {
    /// Create a new `IpLinkDeviceState` with a link-specific state `link`.
    pub(crate) fn new(link: D) -> Self {
        Self { ip: DualStackIpDeviceState::default(), link }
    }
}

/// The various states an IP address can be on an interface.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum AddressState {
    /// The address is assigned to an interface and can be considered bound to
    /// it (all packets destined to the address will be accepted).
    Assigned,

    /// The address is considered unassigned to an interface for normal
    /// operations, but has the intention of being assigned in the future (e.g.
    /// once NDP's Duplicate Address Detection is completed).
    ///
    /// When `dad_transmits_remaining` is `None`, then no more DAD messages need
    /// to be sent and DAD may be resolved.
    Tentative { dad_transmits_remaining: Option<NonZeroU8> },

    /// The address is considered deprecated on an interface. Existing
    /// connections using the address will be fine, however new connections
    /// should not use the deprecated address.
    Deprecated,
}

impl AddressState {
    /// Is this address assigned?
    pub(crate) fn is_assigned(self) -> bool {
        self == AddressState::Assigned
    }

    /// Is this address tentative?
    pub(crate) fn is_tentative(self) -> bool {
        match self {
            AddressState::Assigned | AddressState::Deprecated => false,
            AddressState::Tentative { dad_transmits_remaining: _ } => true,
        }
    }

    /// Is this address deprecated?
    pub(crate) fn is_deprecated(self) -> bool {
        self == AddressState::Deprecated
    }
}

/// Configuration for an IPv6 address assigned via SLAAC.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct SlaacConfig<Instant> {
    /// The lifetime of the address, or `None` for a link-local address.
    pub valid_until: Option<Instant>,
    // TODO(https://fxbug.dev/21428) split this into Static and Temporary enum.
}

/// The configuration for an IPv6 address.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum AddrConfig<Instant> {
    /// Configured by stateless address autoconfiguration.
    Slaac(SlaacConfig<Instant>),

    /// Manually configured.
    Manual,
}

impl<Instant> AddrConfig<Instant> {
    /// The configuration for a link-local address configured via SLAAC.
    ///
    /// Per [RFC 4862 Section 5.3]: "A link-local address has an infinite preferred and valid
    /// lifetime; it is never timed out."
    ///
    /// [RFC 4862 Section 5.3]: https://tools.ietf.org/html/rfc4862#section-5.3
    pub(crate) const SLAAC_LINK_LOCAL: Self = Self::Slaac(SlaacConfig { valid_until: None });

    /// Constructs a new `AddrConfig` describing a global address configured via SLAAC.
    ///
    /// According to the algorithm specified in [RFC 4862 Section 5.5.3.e], a global address
    /// determined via SLAAC will always have a non-infinite valid lifetime.
    ///
    /// [RFC 4862 Section 5.5.3.e]: https://tools.ietf.org/html/rfc4862#section-5.5.3
    pub(crate) const fn new_slaac_global(valid_until: Instant) -> Self {
        Self::Slaac(SlaacConfig { valid_until: Some(valid_until) })
    }
}

/// The type of address configuration.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum AddrConfigType {
    /// Configured by stateless address autoconfiguration.
    Slaac,

    /// Manually configured.
    Manual,
}

/// Data associated with an IPv6 address on an interface.
// TODO(https://fxbug.dev/91753): Should this be generalized for loopback?
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub(crate) struct Ipv6AddressEntry<Instant> {
    pub(crate) addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
    pub(crate) state: AddressState,
    pub(crate) config: AddrConfig<Instant>,
}

impl<Instant> Ipv6AddressEntry<Instant> {
    pub(crate) fn new(
        addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
        state: AddressState,
        config: AddrConfig<Instant>,
    ) -> Self {
        Self { addr_sub, state, config }
    }

    pub(crate) fn addr_sub(&self) -> &AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>> {
        &self.addr_sub
    }

    pub(crate) fn config_type(&self) -> AddrConfigType {
        match self.config {
            AddrConfig::Slaac(_) => AddrConfigType::Slaac,
            AddrConfig::Manual => AddrConfigType::Manual,
        }
    }

    pub(crate) fn mark_permanent(&mut self) {
        self.state = AddressState::Assigned;
    }

    pub(crate) fn is_global(&self) -> bool {
        self.addr_sub().addr().scope() == Ipv6Scope::Global
    }
}

impl<Instant: Copy> Ipv6AddressEntry<Instant> {
    pub(crate) fn valid_until(&self) -> Option<Instant> {
        match &self.config {
            AddrConfig::Slaac(SlaacConfig { valid_until }) => *valid_until,
            AddrConfig::Manual => None,
        }
    }
}
