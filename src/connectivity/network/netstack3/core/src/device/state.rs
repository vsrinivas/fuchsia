// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! State maintained by the device layer.

use alloc::vec::Vec;
use core::{fmt::Debug, num::NonZeroU8, ops::Deref};

use net_types::{
    ip::{AddrSubnet, IpAddress, Ipv4, Ipv6, Ipv6Addr, Ipv6Scope},
    ScopeableAddress as _, SpecifiedAddr, UnicastAddr, Witness,
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

impl<I: Instant> IpDeviceState<I, Ipv6> {
    /// Iterates over the global IPv6 address entries.
    pub(crate) fn iter_global_ipv6_addrs(
        &self,
    ) -> impl Iterator<Item = &AddressEntry<Ipv6Addr, I, UnicastAddr<Ipv6Addr>>> + Clone {
        self.addrs.iter().filter(|entry| entry.is_global())
    }

    /// Iterates mutably over the global IPv6 address entries.
    pub(crate) fn iter_global_ipv6_addrs_mut(
        &mut self,
    ) -> impl Iterator<Item = &mut AddressEntry<Ipv6Addr, I, UnicastAddr<Ipv6Addr>>> {
        self.addrs.iter_mut().filter(|entry| entry.is_global())
    }
}

/// IPv4 and IPv6 state combined.
pub(crate) struct DualStackIpDeviceState<I: Instant> {
    /// IPv4 state.
    pub ipv4: IpDeviceState<I, Ipv4>,

    /// IPv6 state.
    pub ipv6: IpDeviceState<I, Ipv6>,
}

impl<I: Instant> Default for DualStackIpDeviceState<I> {
    fn default() -> DualStackIpDeviceState<I> {
        DualStackIpDeviceState { ipv4: IpDeviceState::default(), ipv6: IpDeviceState::default() }
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
pub enum AddressState {
    /// The address is assigned to an interface and can be considered bound to
    /// it (all packets destined to the address will be accepted).
    Assigned,

    /// The address is considered unassigned to an interface for normal
    /// operations, but has the intention of being assigned in the future (e.g.
    /// once NDP's Duplicate Address Detection is completed).
    Tentative,

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
        self == AddressState::Tentative
    }

    /// Is this address deprecated?
    pub(crate) fn is_deprecated(self) -> bool {
        self == AddressState::Deprecated
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

/// Data associated with an IP address on an interface.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct AddressEntry<S: IpAddress, Instant, A: Witness<S> + Copy = SpecifiedAddr<S>> {
    addr_sub: AddrSubnet<S, A>,
    pub state: AddressState,
    config_type: AddrConfigType,
    pub valid_until: Option<Instant>,
}

impl<S: IpAddress, Instant, A: Witness<S> + Copy> AddressEntry<S, Instant, A> {
    pub(crate) fn new(
        addr_sub: AddrSubnet<S, A>,
        state: AddressState,
        config_type: AddrConfigType,
        valid_until: Option<Instant>,
    ) -> Self {
        Self { addr_sub, state, config_type, valid_until }
    }

    pub(crate) fn addr_sub(&self) -> &AddrSubnet<S, A> {
        &self.addr_sub
    }

    pub(crate) fn config_type(&self) -> AddrConfigType {
        self.config_type
    }

    pub(crate) fn mark_permanent(&mut self) {
        self.state = AddressState::Assigned;
    }
}

impl<Instant, A: Witness<Ipv6Addr> + Deref<Target = Ipv6Addr> + Copy>
    AddressEntry<Ipv6Addr, Instant, A>
{
    pub(crate) fn is_global(&self) -> bool {
        self.addr_sub().addr().scope() == Ipv6Scope::Global
    }
}
