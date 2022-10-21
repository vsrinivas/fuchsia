// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! State for an IP device.

use alloc::vec::Vec;
use core::{fmt::Debug, num::NonZeroU8, time::Duration};

use derivative::Derivative;
use net_types::{
    ip::{AddrSubnet, GenericOverIp, Ip, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    SpecifiedAddr, UnicastAddr, Witness,
};
use nonzero_ext::nonzero;
use packet_formats::utils::NonZeroDuration;

use crate::{
    ip::{
        device::{route_discovery::Ipv6RouteDiscoveryState, slaac::SlaacConfiguration},
        gmp::{igmp::IgmpGroupState, mld::MldGroupState, MulticastGroupSet},
    },
    sync::RwLock,
    Instant,
};

/// The default value for *RetransTimer* as defined in [RFC 4861 section 10].
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
const RETRANS_TIMER_DEFAULT: NonZeroDuration = NonZeroDuration::from_nonzero_secs(nonzero!(1u64));

/// The default value for the default hop limit to be used when sending IP
/// packets.
const DEFAULT_HOP_LIMIT: NonZeroU8 = nonzero!(64u8);

pub(crate) enum DelIpv6AddrReason {
    ManualAction,
    DadFailed,
}

/// An `Ip` extension trait adding IP device state properties.
pub(crate) trait IpDeviceStateIpExt: Ip {
    /// The information stored about an IP address assigned to an interface.
    type AssignedAddress<I: Instant>: AssignedAddress<Self::Addr> + Clone + Debug;

    /// The state kept by the Group Messaging Protocol (GMP) used to announce
    /// membership in an IP multicast group for this version of IP.
    ///
    /// Note that a GMP is only used when membership must be explicitly
    /// announced. For example, a GMP is not used in the context of a loopback
    /// device (because there are no remote hosts) or in the context of an IPsec
    /// device (because multicast is not supported).
    type GmpState<I: Instant>;

    /// Examines the address and returns its subnet if it is assigned.
    ///
    /// Otherwise returns `None`.
    fn assigned_addr<I: Instant>(addr: &Self::AssignedAddress<I>)
        -> Option<AddrSubnet<Self::Addr>>;
}

impl IpDeviceStateIpExt for Ipv4 {
    type AssignedAddress<I: Instant> = AddrSubnet<Ipv4Addr>;
    type GmpState<I: Instant> = IgmpGroupState<I>;

    fn assigned_addr<I: Instant>(
        addr: &Self::AssignedAddress<I>,
    ) -> Option<AddrSubnet<Self::Addr>> {
        Some(addr.clone())
    }
}

impl IpDeviceStateIpExt for Ipv6 {
    type AssignedAddress<I: Instant> = Ipv6AddressEntry<I>;
    type GmpState<I: Instant> = MldGroupState<I>;

    fn assigned_addr<I: Instant>(
        addr: &Self::AssignedAddress<I>,
    ) -> Option<AddrSubnet<Self::Addr>> {
        // Tentative IP addresses (addresses which are not yet fully bound to a
        // device) and deprecated IP addresses (addresses which have been
        // assigned but should no longer be used for new connections) will not
        // be returned.
        addr.state.is_assigned().then_some((*addr.addr_sub()).to_witness())
    }
}

/// The state associated with an IP address assigned to an IP device.
pub trait AssignedAddress<A: IpAddress> {
    /// Gets the address.
    fn addr(&self) -> SpecifiedAddr<A>;
}

impl AssignedAddress<Ipv4Addr> for AddrSubnet<Ipv4Addr> {
    fn addr(&self) -> SpecifiedAddr<Ipv4Addr> {
        self.addr()
    }
}

impl<I: Instant> AssignedAddress<Ipv6Addr> for Ipv6AddressEntry<I> {
    fn addr(&self) -> SpecifiedAddr<Ipv6Addr> {
        self.addr_sub().addr().into_specified()
    }
}

/// The state common to all IP devices.
#[cfg_attr(test, derive(Debug))]
pub(crate) struct IpDeviceState<Instant: crate::Instant, I: IpDeviceStateIpExt> {
    /// IP addresses assigned to this device.
    ///
    /// IPv6 addresses may be tentative (performing NDP's Duplicate Address
    /// Detection).
    ///
    /// Does not contain any duplicates.
    addrs: Vec<I::AssignedAddress<Instant>>,

    /// Multicast groups this device has joined.
    pub multicast_groups: MulticastGroupSet<I::Addr, I::GmpState<Instant>>,

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

impl<Instant: crate::Instant, I: IpDeviceStateIpExt> Default for IpDeviceState<Instant, I> {
    fn default() -> IpDeviceState<Instant, I> {
        IpDeviceState {
            addrs: Vec::default(),
            multicast_groups: MulticastGroupSet::default(),
            default_hop_limit: DEFAULT_HOP_LIMIT,
            routing_enabled: false,
        }
    }
}

impl<I: IpDeviceStateIpExt, Instant: crate::Instant, NewIp: IpDeviceStateIpExt> GenericOverIp<NewIp>
    for IpDeviceState<Instant, I>
{
    type Type = IpDeviceState<Instant, NewIp>;
}

// TODO(https://fxbug.dev/84871): Once we figure out what invariants we want to
// hold regarding the set of IP addresses assigned to a device, ensure that all
// of the methods on `IpDeviceState` uphold those invariants.
impl<Instant: crate::Instant, I: IpDeviceStateIpExt> IpDeviceState<Instant, I> {
    /// Iterates over the addresses assigned to this device.
    pub(crate) fn iter_addrs(
        &self,
    ) -> impl ExactSizeIterator<Item = &I::AssignedAddress<Instant>> + ExactSizeIterator + Clone
    {
        self.addrs.iter()
    }

    /// Iterates mutably over the addresses assigned to this device.
    pub(crate) fn iter_addrs_mut(
        &mut self,
    ) -> impl ExactSizeIterator<Item = &mut I::AssignedAddress<Instant>> + ExactSizeIterator {
        self.addrs.iter_mut()
    }

    /// Finds the entry for `addr` if any.
    pub(crate) fn find_addr(&self, addr: &I::Addr) -> Option<&I::AssignedAddress<Instant>> {
        self.addrs.iter().find(|entry| &entry.addr().get() == addr)
    }

    /// Finds the mutable entry for `addr` if any.
    pub(crate) fn find_addr_mut(
        &mut self,
        addr: &I::Addr,
    ) -> Option<&mut I::AssignedAddress<Instant>> {
        self.addrs.iter_mut().find(|entry| &entry.addr().get() == addr)
    }

    /// Adds an IP address to this interface.
    pub(crate) fn add_addr(
        &mut self,
        addr: I::AssignedAddress<Instant>,
    ) -> Result<(), crate::error::ExistsError> {
        if self.iter_addrs().any(|a| a.addr() == addr.addr()) {
            return Err(crate::error::ExistsError);
        }

        Ok(self.addrs.push(addr))
    }

    /// Removes the address.
    pub(crate) fn remove_addr(
        &mut self,
        addr: &I::Addr,
    ) -> Result<I::AssignedAddress<Instant>, crate::error::NotFoundError> {
        let (index, _entry): (_, &I::AssignedAddress<Instant>) = self
            .addrs
            .iter()
            .enumerate()
            .find(|(_, entry)| &entry.addr().get() == addr)
            .ok_or(crate::error::NotFoundError)?;
        Ok(self.addrs.remove(index))
    }
}

/// The state common to all IPv4 devices.
pub(crate) struct Ipv4DeviceState<I: Instant> {
    pub(crate) ip_state: IpDeviceState<I, Ipv4>,
    pub(super) config: Ipv4DeviceConfiguration,
}

impl<I: Instant> Default for Ipv4DeviceState<I> {
    fn default() -> Ipv4DeviceState<I> {
        Ipv4DeviceState { ip_state: Default::default(), config: Default::default() }
    }
}

impl<I: Instant> AsRef<IpDeviceState<I, Ipv4>> for Ipv4DeviceState<I> {
    fn as_ref(&self) -> &IpDeviceState<I, Ipv4> {
        &self.ip_state
    }
}

impl<I: Instant> AsMut<IpDeviceState<I, Ipv4>> for Ipv4DeviceState<I> {
    fn as_mut(&mut self) -> &mut IpDeviceState<I, Ipv4> {
        &mut self.ip_state
    }
}

impl<I: Instant> AsRef<IpDeviceConfiguration> for Ipv4DeviceState<I> {
    fn as_ref(&self) -> &IpDeviceConfiguration {
        &self.config.ip_config
    }
}

/// Configurations common to all IP devices.
#[derive(Copy, Clone, Debug, Default)]
pub struct IpDeviceConfiguration {
    /// Is IP enabled for this device.
    pub ip_enabled: bool,

    /// Is a Group Messaging Protocol (GMP) enabled for this device?
    ///
    /// If `gmp_enabled` is false, multicast groups will still be added to
    /// `multicast_groups`, but we will not inform the network of our membership
    /// in those groups using a GMP.
    ///
    /// Default: `false`.
    pub gmp_enabled: bool,
}

/// Configuration common to all IPv4 devices.
#[derive(Copy, Clone, Debug, Default)]
pub struct Ipv4DeviceConfiguration {
    /// The configuration common to all IP devices.
    pub ip_config: IpDeviceConfiguration,
}

/// Configuration common to all IPv6 devices.
#[derive(Copy, Clone, Debug, Default)]
pub struct Ipv6DeviceConfiguration {
    /// The value for NDP's DupAddrDetectTransmits parameter as defined by
    /// [RFC 4862 section 5.1].
    ///
    /// A value of `None` means DAD will not be performed on the interface.
    ///
    /// [RFC 4862 section 5.1]: https://datatracker.ietf.org/doc/html/rfc4862#section-5.1
    pub dad_transmits: Option<NonZeroU8>,

    /// Value for NDP's `MAX_RTR_SOLICITATIONS` parameter to configure how many
    /// router solicitation messages to send when solicing routers.
    ///
    /// A value of `None` means router solicitation will not be performed.
    ///
    /// See [RFC 4861 section 6.3.7] for details.
    ///
    /// [RFC 4861 section 6.3.7]: https://datatracker.ietf.org/doc/html/rfc4861#section-6.3.7
    pub max_router_solicitations: Option<NonZeroU8>,

    /// The configuration for SLAAC.
    pub slaac_config: SlaacConfiguration,

    /// The configuration common to all IP devices.
    pub ip_config: IpDeviceConfiguration,
}

/// The state common to all IPv6 devices.
pub(crate) struct Ipv6DeviceState<I: Instant> {
    /// The time between retransmissions of Neighbor Solicitation messages to a
    /// neighbor when resolving the address or when probing the reachability of
    /// a neighbor.
    ///
    /// Default: [`RETRANS_TIMER_DEFAULT`].
    ///
    /// See RetransTimer in [RFC 4861 section 6.3.2] for more details.
    ///
    /// [RFC 4861 section 6.3.2]: https://tools.ietf.org/html/rfc4861#section-6.3.2
    pub(crate) retrans_timer: NonZeroDuration,
    pub(super) route_discovery: Ipv6RouteDiscoveryState,
    pub(super) router_soliciations_remaining: Option<NonZeroU8>,
    pub(crate) ip_state: IpDeviceState<I, Ipv6>,
    pub(crate) config: Ipv6DeviceConfiguration,
}

impl<I: Instant> Default for Ipv6DeviceState<I> {
    fn default() -> Ipv6DeviceState<I> {
        Ipv6DeviceState {
            retrans_timer: RETRANS_TIMER_DEFAULT,
            route_discovery: Default::default(),
            router_soliciations_remaining: None,
            ip_state: Default::default(),
            config: Default::default(),
        }
    }
}

impl<I: Instant> AsRef<IpDeviceState<I, Ipv6>> for Ipv6DeviceState<I> {
    fn as_ref(&self) -> &IpDeviceState<I, Ipv6> {
        &self.ip_state
    }
}

impl<I: Instant> AsMut<IpDeviceState<I, Ipv6>> for Ipv6DeviceState<I> {
    fn as_mut(&mut self) -> &mut IpDeviceState<I, Ipv6> {
        &mut self.ip_state
    }
}

impl<I: Instant> AsRef<IpDeviceConfiguration> for Ipv6DeviceState<I> {
    fn as_ref(&self) -> &IpDeviceConfiguration {
        &self.config.ip_config
    }
}

/// IPv4 and IPv6 state combined.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct DualStackIpDeviceState<I: Instant> {
    /// IPv4 state.
    pub ipv4: RwLock<Ipv4DeviceState<I>>,

    /// IPv6 state.
    pub ipv6: RwLock<Ipv6DeviceState<I>>,
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
}

impl AddressState {
    /// Is this address assigned?
    pub(crate) fn is_assigned(self) -> bool {
        self == AddressState::Assigned
    }

    /// Is this address tentative?
    pub(crate) fn is_tentative(self) -> bool {
        match self {
            AddressState::Assigned => false,
            AddressState::Tentative { dad_transmits_remaining: _ } => true,
        }
    }
}

/// Configuration for a temporary IPv6 address assigned via SLAAC.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct TemporarySlaacConfig<Instant> {
    /// The time at which the address is no longer valid.
    pub(crate) valid_until: Instant,
    /// The per-address DESYNC_FACTOR specified in RFC 8981 Section 3.4.
    pub(crate) desync_factor: Duration,
    /// The time at which the address was created.
    pub(crate) creation_time: Instant,
    /// The DAD_Counter parameter specified by RFC 8981 Section 3.3.2.1. This is
    /// used to track the number of retries that occurred prior to picking this
    /// address.
    pub(crate) dad_counter: u8,
}

/// A lifetime that may be forever/infinite.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub(crate) enum Lifetime<I> {
    Finite(I),
    Infinite,
}

/// Configuration for an IPv6 address assigned via SLAAC.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum SlaacConfig<Instant> {
    /// The address is static.
    Static {
        /// The lifetime of the address.
        valid_until: Lifetime<Instant>,
    },
    /// The address is a temporary address, as specified by [RFC 8981].
    ///
    /// [RFC 8981]: https://tools.ietf.org/html/rfc8981
    Temporary(TemporarySlaacConfig<Instant>),
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
    pub(crate) const SLAAC_LINK_LOCAL: Self =
        Self::Slaac(SlaacConfig::Static { valid_until: Lifetime::Infinite });
}

/// Data associated with an IPv6 address on an interface.
// TODO(https://fxbug.dev/91753): Should this be generalized for loopback?
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub(crate) struct Ipv6AddressEntry<Instant> {
    pub(crate) addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
    pub(crate) state: AddressState,
    pub(crate) config: AddrConfig<Instant>,
    pub(crate) deprecated: bool,
}

impl<Instant> Ipv6AddressEntry<Instant> {
    pub(crate) fn new(
        addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
        state: AddressState,
        config: AddrConfig<Instant>,
    ) -> Self {
        Self { addr_sub, state, config, deprecated: false }
    }

    pub(crate) fn addr_sub(&self) -> &AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>> {
        &self.addr_sub
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{context::testutil::FakeInstant, error::ExistsError};

    #[test]
    fn test_add_addr_ipv4() {
        const ADDRESS: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
        const PREFIX_LEN: u8 = 8;

        let mut ipv4 = IpDeviceState::<FakeInstant, Ipv4>::default();

        assert_eq!(ipv4.add_addr(AddrSubnet::new(ADDRESS, PREFIX_LEN).unwrap()), Ok(()));
        // Adding the same address with different prefix should fail.
        assert_eq!(
            ipv4.add_addr(AddrSubnet::new(ADDRESS, PREFIX_LEN + 1).unwrap()),
            Err(ExistsError)
        );
    }

    #[test]
    fn test_add_addr_ipv6() {
        const ADDRESS: Ipv6Addr =
            Ipv6Addr::from_bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6]);
        const PREFIX_LEN: u8 = 8;

        let mut ipv6 = IpDeviceState::<FakeInstant, Ipv6>::default();

        assert_eq!(
            ipv6.add_addr(Ipv6AddressEntry::new(
                AddrSubnet::new(ADDRESS, PREFIX_LEN).unwrap(),
                AddressState::Tentative { dad_transmits_remaining: None },
                AddrConfig::Slaac(SlaacConfig::Static { valid_until: Lifetime::Infinite }),
            )),
            Ok(())
        );
        // Adding the same address with different prefix and configuration
        // should fail.
        assert_eq!(
            ipv6.add_addr(Ipv6AddressEntry::new(
                AddrSubnet::new(ADDRESS, PREFIX_LEN + 1).unwrap(),
                AddressState::Assigned,
                AddrConfig::Manual,
            )),
            Err(ExistsError)
        );
    }
}
