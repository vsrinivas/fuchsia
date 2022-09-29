// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::{
    convert::Infallible as Never,
    num::{NonZeroU16, NonZeroU64},
};

use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_stack as fidl_net_stack;
use fidl_fuchsia_posix as fposix;
use fidl_fuchsia_posix_socket as fposix_socket;
use net_types::{
    ethernet::Mac,
    ip::{
        AddrSubnetEither, AddrSubnetError, IpAddr, IpAddress, Ipv4Addr, Ipv6Addr, SubnetEither,
        SubnetError,
    },
    AddrAndZone, ZonedAddr,
};
use net_types::{SpecifiedAddr, Witness};
use netstack3_core::{
    device::DeviceId,
    error::{ExistsError, NetstackError, NotFoundError},
    ip::{
        forwarding::AddRouteError,
        types::{AddableEntry, AddableEntryEither, EntryEither},
    },
    socket::datagram::{MulticastInterfaceSelector, MulticastMembershipInterfaceSelector},
};

use crate::bindings::socket::{IntoErrno, IpSockAddrExt, SockAddr};

/// A core type which can be fallibly converted from the FIDL type `F`.
///
/// For all `C: TryFromFidl<F>`, we provide a blanket impl of [`F:
/// TryIntoCore<C>`].
///
/// [`F: TryIntoCore<C>`]: crate::bindings::util::TryIntoCore
pub(crate) trait TryFromFidl<F>: Sized {
    /// The type of error returned from [`try_from_fidl`].
    ///
    /// [`try_from_fidl`]: crate::bindings::util::TryFromFidl::try_from_fidl
    type Error;

    /// Attempt to convert from `fidl` into an instance of `Self`.
    fn try_from_fidl(fidl: F) -> Result<Self, Self::Error>;
}

/// A core type which can be fallibly converted to the FIDL type `F`.
///
/// For all `C: TryIntoFidl<F>`, we provide a blanket impl of [`F:
/// TryFromCore<C>`].
///
/// [`F: TryFromCore<C>`]: crate::bindings::util::TryFromCore
pub(crate) trait TryIntoFidl<F>: Sized {
    /// The type of error returned from [`try_into_fidl`].
    ///
    /// [`try_into_fidl`]: crate::bindings::util::TryIntoFidl::try_into_fidl
    type Error;

    /// Attempt to convert `self` into an instance of `F`.
    fn try_into_fidl(self) -> Result<F, Self::Error>;
}

/// A core type which can be infallibly converted into the FIDL type `F`.
///
/// `IntoFidl<F>` extends [`TryIntoFidl<F, Error = Never>`], and provides the
/// infallible conversion method [`into_fidl`].
///
/// [`TryIntoFidl<F, Error = Never>`]: crate::bindings::util::TryIntoFidl
/// [`into_fidl`]: crate::bindings::util::IntoFidl::into_fidl
pub(crate) trait IntoFidl<F>: TryIntoFidl<F, Error = Never> {
    /// Infallibly convert `self` into an instance of `F`.
    fn into_fidl(self) -> F {
        match self.try_into_fidl() {
            Ok(f) => f,
            Err(never) => match never {},
        }
    }
}

impl<F, T: TryIntoFidl<F, Error = Never>> IntoFidl<F> for T {}

/// A FIDL type which can be fallibly converted from the core type `C`.
///
/// `TryFromCore<C>` is automatically implemented for all `F` where [`C:
/// TryIntoFidl<F>`].
///
/// [`C: TryIntoFidl<F>`]: crate::bindings::util::TryIntoFidl
pub(crate) trait TryFromCore<C>: Sized
where
    C: TryIntoFidl<Self>,
{
    /// Attempt to convert from `core` into an instance of `Self`.
    fn try_from_core(core: C) -> Result<Self, C::Error> {
        core.try_into_fidl()
    }
}

impl<F, C: TryIntoFidl<F>> TryFromCore<C> for F {}

/// A FIDL type which can be fallibly converted into the core type `C`.
///
/// `TryIntoCore<C>` is automatically implemented for all `F` where [`C:
/// TryFromFidl<F>`].
///
/// [`C: TryFromFidl<F>`]: crate::bindings::util::TryFromFidl
pub(crate) trait TryIntoCore<C>: Sized
where
    C: TryFromFidl<Self>,
{
    /// Attempt to convert from `self` into an instance of `C`.
    ///
    /// This is equivalent to [`C::try_from_fidl(self)`].
    ///
    /// [`C::try_from_fidl(self)`]: crate::bindings::util::TryFromFidl::try_from_fidl
    fn try_into_core(self) -> Result<C, C::Error> {
        C::try_from_fidl(self)
    }
}

impl<F, C: TryFromFidl<F>> TryIntoCore<C> for F {}

/// A FIDL type which can be infallibly converted into the core type `C`.
///
/// `IntoCore<C>` extends [`TryIntoCore<C>`] where `<C as TryFromFidl<_>>::Error
/// = Never`, and provides the infallible conversion method [`into_core`].
///
/// [`TryIntoCore<C>`]: crate::bindings::util::TryIntoCore
/// [`into_fidl`]: crate::bindings::util::IntoCore::into_core
pub(crate) trait IntoCore<C>: TryIntoCore<C>
where
    C: TryFromFidl<Self, Error = Never>,
{
    /// Infallibly convert `self` into an instance of `C`.
    fn into_core(self) -> C {
        match self.try_into_core() {
            Ok(c) => c,
            Err(never) => match never {},
        }
    }
}

impl<F, C: TryFromFidl<F, Error = Never>> IntoCore<C> for F {}

impl<T> TryIntoFidl<T> for Never {
    type Error = Never;

    fn try_into_fidl(self) -> Result<T, Never> {
        match self {}
    }
}

impl TryIntoFidl<fidl_net_stack::Error> for SubnetError {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net_stack::Error, Never> {
        Ok(fidl_net_stack::Error::InvalidArgs)
    }
}

impl TryIntoFidl<fidl_net_stack::Error> for AddrSubnetError {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net_stack::Error, Never> {
        Ok(fidl_net_stack::Error::InvalidArgs)
    }
}

impl TryIntoFidl<fidl_net_stack::Error> for ExistsError {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net_stack::Error, Never> {
        Ok(fidl_net_stack::Error::AlreadyExists)
    }
}

impl TryIntoFidl<fidl_net_stack::Error> for NetstackError {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net_stack::Error, Never> {
        match self {
            NetstackError::Exists => Ok(fidl_net_stack::Error::AlreadyExists),
            NetstackError::NotFound => Ok(fidl_net_stack::Error::NotFound),
            _ => Ok(fidl_net_stack::Error::Internal),
        }
    }
}

impl TryIntoFidl<fidl_net_stack::Error> for NotFoundError {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net_stack::Error, Never> {
        Ok(fidl_net_stack::Error::NotFound)
    }
}

impl TryIntoFidl<fidl_net_stack::Error> for AddRouteError {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net_stack::Error, Never> {
        match self {
            AddRouteError::AlreadyExists => Ok(fidl_net_stack::Error::AlreadyExists),
            AddRouteError::GatewayNotNeighbor => Ok(fidl_net_stack::Error::BadState),
        }
    }
}

impl TryFromFidl<fidl_net::IpAddress> for IpAddr {
    type Error = Never;

    fn try_from_fidl(addr: fidl_net::IpAddress) -> Result<IpAddr, Never> {
        match addr {
            fidl_net::IpAddress::Ipv4(v4) => Ok(IpAddr::V4(v4.into_core())),
            fidl_net::IpAddress::Ipv6(v6) => Ok(IpAddr::V6(v6.into_core())),
        }
    }
}

impl TryIntoFidl<fidl_net::IpAddress> for IpAddr {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net::IpAddress, Never> {
        match self {
            IpAddr::V4(addr) => Ok(fidl_net::IpAddress::Ipv4(addr.into_fidl())),
            IpAddr::V6(addr) => Ok(fidl_net::IpAddress::Ipv6(addr.into_fidl())),
        }
    }
}

impl TryFromFidl<fidl_net::Ipv4Address> for Ipv4Addr {
    type Error = Never;

    fn try_from_fidl(addr: fidl_net::Ipv4Address) -> Result<Ipv4Addr, Never> {
        Ok(addr.addr.into())
    }
}

impl TryIntoFidl<fidl_net::Ipv4Address> for Ipv4Addr {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net::Ipv4Address, Never> {
        Ok(fidl_net::Ipv4Address { addr: self.ipv4_bytes() })
    }
}

impl TryFromFidl<fidl_net::Ipv6Address> for Ipv6Addr {
    type Error = Never;

    fn try_from_fidl(addr: fidl_net::Ipv6Address) -> Result<Ipv6Addr, Never> {
        Ok(addr.addr.into())
    }
}

impl TryIntoFidl<fidl_net::Ipv6Address> for Ipv6Addr {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net::Ipv6Address, Never> {
        Ok(fidl_net::Ipv6Address { addr: self.ipv6_bytes() })
    }
}

impl TryFromFidl<fidl_net::MacAddress> for Mac {
    type Error = Never;

    fn try_from_fidl(mac: fidl_net::MacAddress) -> Result<Mac, Never> {
        Ok(Mac::new(mac.octets))
    }
}

impl TryIntoFidl<fidl_net::MacAddress> for Mac {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net::MacAddress, Never> {
        Ok(fidl_net::MacAddress { octets: self.bytes() })
    }
}

/// An error indicating that an address was a member of the wrong class (for
/// example, a unicast address used where a multicast address is required).
#[derive(Debug)]
pub struct AddrClassError;

// TODO(joshlf): Introduce a separate variant to `fidl_net_stack::Error` for
// `AddrClassError`?
impl TryIntoFidl<fidl_net_stack::Error> for AddrClassError {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net_stack::Error, Never> {
        Ok(fidl_net_stack::Error::InvalidArgs)
    }
}

impl TryFromFidl<fidl_net::IpAddress> for SpecifiedAddr<IpAddr> {
    type Error = AddrClassError;

    fn try_from_fidl(fidl: fidl_net::IpAddress) -> Result<SpecifiedAddr<IpAddr>, AddrClassError> {
        SpecifiedAddr::new(fidl.into_core()).ok_or(AddrClassError)
    }
}

impl TryIntoFidl<fidl_net::IpAddress> for SpecifiedAddr<IpAddr> {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net::IpAddress, Never> {
        Ok(self.get().into_fidl())
    }
}

impl TryFromFidl<fidl_net::Subnet> for AddrSubnetEither {
    type Error = AddrSubnetError;

    fn try_from_fidl(fidl: fidl_net::Subnet) -> Result<AddrSubnetEither, AddrSubnetError> {
        AddrSubnetEither::new(fidl.addr.into_core(), fidl.prefix_len)
    }
}

impl TryIntoFidl<fidl_net::Subnet> for AddrSubnetEither {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net::Subnet, Never> {
        let (addr, prefix) = self.addr_prefix();
        Ok(fidl_net::Subnet { addr: addr.into_fidl(), prefix_len: prefix })
    }
}

impl TryFromFidl<fidl_net::Subnet> for SubnetEither {
    type Error = SubnetError;

    fn try_from_fidl(fidl: fidl_net::Subnet) -> Result<SubnetEither, SubnetError> {
        SubnetEither::new(fidl.addr.into_core(), fidl.prefix_len)
    }
}

impl TryIntoFidl<fidl_net::Subnet> for SubnetEither {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fidl_net::Subnet, Never> {
        let (net, prefix) = self.net_prefix();
        Ok(fidl_net::Subnet { addr: net.into_fidl(), prefix_len: prefix })
    }
}

impl TryFromFidl<fposix_socket::OptionalUint8> for Option<u8> {
    type Error = Never;

    fn try_from_fidl(fidl: fposix_socket::OptionalUint8) -> Result<Self, Self::Error> {
        Ok(match fidl {
            fposix_socket::OptionalUint8::Unset(fposix_socket::Empty) => None,
            fposix_socket::OptionalUint8::Value(u) => Some(u),
        })
    }
}

impl TryIntoFidl<fposix_socket::OptionalUint8> for Option<u8> {
    type Error = Never;

    fn try_into_fidl(self) -> Result<fposix_socket::OptionalUint8, Self::Error> {
        Ok(self
            .map(fposix_socket::OptionalUint8::Value)
            .unwrap_or(fposix_socket::OptionalUint8::Unset(fposix_socket::Empty)))
    }
}

impl TryIntoFidl<fnet_interfaces_admin::AddressAssignmentState>
    for netstack3_core::ip::device::IpAddressState
{
    type Error = Never;

    fn try_into_fidl(self) -> Result<fnet_interfaces_admin::AddressAssignmentState, Never> {
        match self {
            netstack3_core::ip::device::IpAddressState::Unavailable => {
                Ok(fnet_interfaces_admin::AddressAssignmentState::Unavailable)
            }
            netstack3_core::ip::device::IpAddressState::Assigned => {
                Ok(fnet_interfaces_admin::AddressAssignmentState::Assigned)
            }
            netstack3_core::ip::device::IpAddressState::Tentative => {
                Ok(fnet_interfaces_admin::AddressAssignmentState::Tentative)
            }
        }
    }
}

impl<A: IpAddress> TryIntoFidl<<A::Version as IpSockAddrExt>::SocketAddress>
    for (Option<SpecifiedAddr<A>>, NonZeroU16)
where
    A::Version: IpSockAddrExt,
{
    type Error = Never;

    fn try_into_fidl(self) -> Result<<A::Version as IpSockAddrExt>::SocketAddress, Self::Error> {
        let (addr, port) = self;
        Ok(SockAddr::new(addr.map(ZonedAddr::Unzoned), port.get()))
    }
}

/// Provides a stateful context for operations that require state-keeping to be
/// completed.
///
/// `ConversionContext` is used by conversion functions in
/// [`TryFromFidlWithContext`] and [`TryFromCoreWithContext`].
pub(crate) trait ConversionContext {
    /// Converts a binding identifier (exposed in FIDL as `u64`) to a core
    /// identifier `DeviceId`.
    ///
    /// Returns `None` if there is no core mapping equivalent for `binding_id`.
    fn get_core_id(&self, binding_id: u64) -> Option<DeviceId>;
    /// Converts a core identifier `DeviceId` to a FIDL-compatible `u64`
    /// identifier.
    ///
    /// Returns `None` if there is no FIDL mapping equivalent for `core_id`.
    fn get_binding_id(&self, core_id: DeviceId) -> Option<u64>;
}

/// A core type which can be fallibly converted from the FIDL type `F` given a
/// context that implements [`ConversionContext`].
///
/// For all `C: TryFromFidlWithContext<F>`, we provide a blanket impl of [`F:
/// TryIntoCoreWithContext<C>`].
///
/// [`F: TryIntoCoreWithContext<C>`]: crate::bindings::util::TryIntoCoreWithContext
pub(crate) trait TryFromFidlWithContext<F>: Sized {
    /// The type of error returned from [`try_from_fidl_with_ctx`].
    ///
    /// [`try_from_fidl_with_ctx`]: crate::bindings::util::TryFromFidlWithContext::try_from_fidl_with_ctx
    type Error;

    /// Attempt to convert from `fidl` into an instance of `Self`.
    fn try_from_fidl_with_ctx<C: ConversionContext>(ctx: &C, fidl: F) -> Result<Self, Self::Error>;
}

/// A core type which can be fallibly converted to the FIDL type `F` given a
/// context that implements [`ConversionContext`].
///
/// For all `C: TryIntoFidlWithContext<F>`, we provide a blanket impl of [`F:
/// TryFromCoreWithContext<C>`].
///
/// [`F: TryFromCoreWithContext<C>`]: crate::bindings::util::TryFromCoreWithContext
pub(crate) trait TryIntoFidlWithContext<F>: Sized {
    /// The type of error returned from [`try_into_fidl_with_ctx`].
    ///
    /// [`try_into_fidl_with_ctx`]: crate::bindings::util::TryIntoFidlWithContext::try_from_fidl_with_ctx
    type Error;

    /// Attempt to convert from `self` into an instance of `F`.
    fn try_into_fidl_with_ctx<C: ConversionContext>(self, ctx: &C) -> Result<F, Self::Error>;
}

/// A FIDL type which can be fallibly converted from the core type `C` given a
/// context that implements [`ConversionContext`].
///
/// `TryFromCoreWithContext<C>` is automatically implemented for all `F` where
/// [`C: TryIntoFidlWithContext<F>`].
///
/// [`C: TryIntoFidlWithContext<F>`]: crate::bindings::util::TryIntoFidlWithContext
pub(crate) trait TryFromCoreWithContext<C>: Sized
where
    C: TryIntoFidlWithContext<Self>,
{
    /// Attempt to convert from `core` into an instance of `Self`.
    fn try_from_core_with_ctx<X: ConversionContext>(ctx: &X, core: C) -> Result<Self, C::Error> {
        core.try_into_fidl_with_ctx(ctx)
    }
}

impl<F, C: TryIntoFidlWithContext<F>> TryFromCoreWithContext<C> for F {}

/// A FIDL type which can be fallibly converted into the core type `C` given a
/// context that implements [`ConversionContext`].
///
/// `TryIntoCoreWithContext<C>` is automatically implemented for all `F` where
/// [`C: TryFromFidlWithContext<F>`].
///
/// [`C: TryFromFidlWithContext<F>`]: crate::bindings::util::TryFromFidlWithContext
pub(crate) trait TryIntoCoreWithContext<C>: Sized
where
    C: TryFromFidlWithContext<Self>,
{
    /// Attempt to convert from `self` into an instance of `C`.
    fn try_into_core_with_ctx<X: ConversionContext>(self, ctx: &X) -> Result<C, C::Error> {
        C::try_from_fidl_with_ctx(ctx, self)
    }
}

impl<F, C: TryFromFidlWithContext<F>> TryIntoCoreWithContext<C> for F {}

#[derive(Debug, PartialEq)]
pub struct DeviceNotFoundError;

impl TryFromFidlWithContext<u64> for DeviceId {
    type Error = DeviceNotFoundError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: u64,
    ) -> Result<DeviceId, DeviceNotFoundError> {
        ctx.get_core_id(fidl).ok_or(DeviceNotFoundError)
    }
}

#[derive(Debug, PartialEq)]
pub enum SocketAddressError {
    Device(DeviceNotFoundError),
    UnexpectedZone,
}

impl IntoErrno for SocketAddressError {
    fn into_errno(self) -> fposix::Errno {
        match self {
            SocketAddressError::Device(d) => d.into_errno(),
            SocketAddressError::UnexpectedZone => todo!(),
        }
    }
}

impl<A: IpAddress, D> TryFromFidlWithContext<<A::Version as IpSockAddrExt>::SocketAddress>
    for (Option<ZonedAddr<A, D>>, u16)
where
    A::Version: IpSockAddrExt,
    D: TryFromFidlWithContext<
        <<A::Version as IpSockAddrExt>::SocketAddress as SockAddr>::Zone,
        Error = DeviceNotFoundError,
    >,
{
    type Error = SocketAddressError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: <A::Version as IpSockAddrExt>::SocketAddress,
    ) -> Result<Self, Self::Error> {
        let port = fidl.port();
        let specified = match fidl.get_specified_addr() {
            Some(addr) => addr,
            None => return Ok((None, port)),
        };

        let zoned = match fidl.zone() {
            Some(zone) => {
                let addr_and_zone = AddrAndZone::new(specified.get(), zone)
                    .ok_or(SocketAddressError::UnexpectedZone)?;

                addr_and_zone
                    .try_map_zone(|zone| {
                        TryFromFidlWithContext::try_from_fidl_with_ctx(ctx, zone)
                            .map_err(SocketAddressError::Device)
                    })
                    .map(Into::into)?
            }
            None => specified.into(),
        };
        Ok((Some(zoned), port))
    }
}

impl<A: IpAddress, D> TryIntoFidlWithContext<<A::Version as IpSockAddrExt>::SocketAddress>
    for (Option<ZonedAddr<A, D>>, u16)
where
    A::Version: IpSockAddrExt,
    D: TryIntoFidlWithContext<
        <<A::Version as IpSockAddrExt>::SocketAddress as SockAddr>::Zone,
        Error = DeviceNotFoundError,
    >,
{
    type Error = DeviceNotFoundError;

    fn try_into_fidl_with_ctx<C: ConversionContext>(
        self,
        ctx: &C,
    ) -> Result<<A::Version as IpSockAddrExt>::SocketAddress, Self::Error> {
        let (addr, port) = self;
        let addr = addr
            .map(|addr| {
                Ok(match addr {
                    ZonedAddr::Unzoned(addr) => addr.into(),
                    ZonedAddr::Zoned(z) => z
                        .try_map_zone(|zone| {
                            TryIntoFidlWithContext::try_into_fidl_with_ctx(zone, ctx)
                        })?
                        .into(),
                })
            })
            .transpose()?;
        Ok(SockAddr::new(addr, port))
    }
}

impl<A, D1, D2> TryFromFidlWithContext<MulticastMembershipInterfaceSelector<A, D1>>
    for MulticastMembershipInterfaceSelector<A, D2>
where
    D2: TryFromFidlWithContext<D1>,
{
    type Error = D2::Error;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        selector: MulticastMembershipInterfaceSelector<A, D1>,
    ) -> Result<Self, Self::Error> {
        use MulticastMembershipInterfaceSelector::*;
        Ok(match selector {
            Specified(MulticastInterfaceSelector::Interface(id)) => {
                Specified(MulticastInterfaceSelector::Interface(id.try_into_core_with_ctx(ctx)?))
            }
            Specified(MulticastInterfaceSelector::LocalAddress(addr)) => {
                Specified(MulticastInterfaceSelector::LocalAddress(addr))
            }
            AnyInterfaceWithRoute => AnyInterfaceWithRoute,
        })
    }
}

impl TryFromFidlWithContext<Never> for DeviceId {
    type Error = DeviceNotFoundError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        _ctx: &C,
        fidl: Never,
    ) -> Result<Self, Self::Error> {
        match fidl {}
    }
}

impl TryIntoFidlWithContext<Never> for DeviceId {
    type Error = DeviceNotFoundError;

    fn try_into_fidl_with_ctx<C: ConversionContext>(self, _ctx: &C) -> Result<Never, Self::Error> {
        Err(DeviceNotFoundError)
    }
}

impl TryFromFidlWithContext<NonZeroU64> for DeviceId {
    type Error = DeviceNotFoundError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: NonZeroU64,
    ) -> Result<Self, Self::Error> {
        TryFromFidlWithContext::try_from_fidl_with_ctx(ctx, fidl.get())
    }
}

impl TryIntoFidlWithContext<u64> for DeviceId {
    type Error = DeviceNotFoundError;

    fn try_into_fidl_with_ctx<C: ConversionContext>(
        self,
        ctx: &C,
    ) -> Result<u64, DeviceNotFoundError> {
        ctx.get_binding_id(self).ok_or(DeviceNotFoundError)
    }
}

impl TryIntoFidlWithContext<NonZeroU64> for DeviceId {
    type Error = DeviceNotFoundError;

    fn try_into_fidl_with_ctx<C: ConversionContext>(
        self,
        ctx: &C,
    ) -> Result<NonZeroU64, DeviceNotFoundError> {
        ctx.get_binding_id(self).and_then(NonZeroU64::new).ok_or(DeviceNotFoundError)
    }
}

impl IntoErrno for DeviceNotFoundError {
    fn into_errno(self) -> fposix::Errno {
        fposix::Errno::Enodev
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum ForwardingConversionError {
    DeviceNotFound,
    TypeMismatch,
    Subnet(SubnetError),
    AddrClassError,
}

impl From<DeviceNotFoundError> for ForwardingConversionError {
    fn from(_: DeviceNotFoundError) -> Self {
        ForwardingConversionError::DeviceNotFound
    }
}

impl From<SubnetError> for ForwardingConversionError {
    fn from(err: SubnetError) -> Self {
        ForwardingConversionError::Subnet(err)
    }
}

impl From<AddrClassError> for ForwardingConversionError {
    fn from(_: AddrClassError) -> Self {
        ForwardingConversionError::AddrClassError
    }
}

impl From<ForwardingConversionError> for fidl_net_stack::Error {
    fn from(fwd_error: ForwardingConversionError) -> Self {
        match fwd_error {
            ForwardingConversionError::DeviceNotFound => fidl_net_stack::Error::NotFound,
            ForwardingConversionError::TypeMismatch
            | ForwardingConversionError::Subnet(_)
            | ForwardingConversionError::AddrClassError => fidl_net_stack::Error::InvalidArgs,
        }
    }
}

impl TryFromFidlWithContext<fidl_net_stack::ForwardingEntry> for AddableEntryEither<DeviceId> {
    type Error = ForwardingConversionError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: fidl_net_stack::ForwardingEntry,
    ) -> Result<AddableEntryEither<DeviceId>, ForwardingConversionError> {
        let fidl_net_stack::ForwardingEntry { subnet, device_id, next_hop, metric: _ } = fidl;
        let subnet = subnet.try_into_core()?;
        let device =
            NonZeroU64::new(device_id).map(|d| d.get().try_into_core_with_ctx(ctx)).transpose()?;
        let next_hop: Option<SpecifiedAddr<IpAddr>> =
            next_hop.map(|next_hop| (*next_hop).try_into_core()).transpose()?;

        Ok(match (subnet, device, next_hop.map(Into::into)) {
            (subnet, Some(device), None) => Self::without_gateway(subnet, device),
            (SubnetEither::V4(subnet), device, Some(IpAddr::V4(gateway))) => {
                AddableEntry::with_gateway(subnet, device, gateway).into()
            }
            (SubnetEither::V6(subnet), device, Some(IpAddr::V6(gateway))) => {
                AddableEntry::with_gateway(subnet, device, gateway).into()
            }
            (SubnetEither::V4(_), _, Some(IpAddr::V6(_)))
            | (SubnetEither::V6(_), _, Some(IpAddr::V4(_)))
            | (_, None, None) => return Err(ForwardingConversionError::TypeMismatch),
        })
    }
}

impl TryIntoFidlWithContext<fidl_net_stack::ForwardingEntry> for EntryEither<DeviceId> {
    type Error = DeviceNotFoundError;

    fn try_into_fidl_with_ctx<C: ConversionContext>(
        self,
        ctx: &C,
    ) -> Result<fidl_net_stack::ForwardingEntry, DeviceNotFoundError> {
        let (subnet, device, gateway) = self.into_subnet_device_gateway();
        let device_id = device.try_into_fidl_with_ctx(ctx)?;
        let next_hop = gateway.map(|next_hop| {
            let next_hop: SpecifiedAddr<IpAddr> = next_hop.into();
            Box::new(next_hop.get().into_fidl())
        });
        Ok(fidl_net_stack::ForwardingEntry {
            subnet: subnet.into_fidl(),
            device_id,
            next_hop,
            metric: 0,
        })
    }
}

/// Returns `Warn` when `e` is closed; otherwise returns `Error`.
///
/// A closed [`fidl::Error`] indicates that the client closed the channel, and
/// the Netstack should not generate errors based on client behavior.
pub(crate) fn fidl_err_log_level(e: &fidl::Error) -> log::Level {
    if e.is_closed() {
        log::Level::Warn
    } else {
        log::Level::Error
    }
}

#[cfg(test)]
mod tests {

    use fidl_fuchsia_net as fidl_net;
    use fidl_fuchsia_net_ext::IntoExt;
    use fuchsia_zircon_status as zx_status;
    use net_declare::{net_ip_v4, net_ip_v6};
    use net_types::{
        ip::{Ipv4Addr, Ipv6Addr},
        UnicastAddr,
    };
    use netstack3_core::Ctx;
    use test_case::test_case;

    use crate::bindings::NetstackContext;

    use super::*;

    struct FakeConversionContext {
        binding: u64,
        core: DeviceId,
        invalid_core: DeviceId,
    }

    impl FakeConversionContext {
        fn new() -> Self {
            // We need a valid context to be able to create DeviceIds, so
            // we just create it, get the device id, and then destroy
            // everything.
            let ctx = NetstackContext::default();
            let mut state = ctx.try_lock().unwrap();
            let state: &mut Ctx<_> = &mut state;

            let [core, invalid_core] = [(); 2].map(|()| {
                netstack3_core::device::add_ethernet_device(
                    &mut state.sync_ctx,
                    &mut state.non_sync_ctx,
                    UnicastAddr::new(Mac::new([2, 3, 4, 5, 6, 7])).unwrap(),
                    1500,
                )
            });

            Self { binding: 1, core, invalid_core }
        }
    }

    impl ConversionContext for FakeConversionContext {
        fn get_core_id(&self, binding_id: u64) -> Option<DeviceId> {
            if binding_id == self.binding {
                Some(self.core)
            } else {
                None
            }
        }

        fn get_binding_id(&self, core_id: DeviceId) -> Option<u64> {
            if self.core == core_id {
                Some(self.binding)
            } else {
                None
            }
        }
    }

    struct EmptyFakeConversionContext;
    impl ConversionContext for EmptyFakeConversionContext {
        fn get_core_id(&self, _binding_id: u64) -> Option<DeviceId> {
            None
        }

        fn get_binding_id(&self, _core_id: DeviceId) -> Option<u64> {
            None
        }
    }

    fn create_addr_v4(bytes: [u8; 4]) -> (IpAddr, fidl_net::IpAddress) {
        let core = IpAddr::V4(Ipv4Addr::from(bytes));
        let fidl = fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: bytes });
        (core, fidl)
    }

    fn create_addr_v6(bytes: [u8; 16]) -> (IpAddr, fidl_net::IpAddress) {
        let core = IpAddr::V6(Ipv6Addr::from(bytes));
        let fidl = fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address { addr: bytes });
        (core, fidl)
    }

    fn create_subnet(
        subnet: (IpAddr, fidl_net::IpAddress),
        prefix: u8,
    ) -> (SubnetEither, fidl_net::Subnet) {
        let (core, fidl) = subnet;
        (
            SubnetEither::new(core, prefix).unwrap(),
            fidl_net::Subnet { addr: fidl, prefix_len: prefix },
        )
    }

    fn create_addr_subnet(
        addr: (IpAddr, fidl_net::IpAddress),
        prefix: u8,
    ) -> (AddrSubnetEither, fidl_net::Subnet) {
        let (core, fidl) = addr;
        (
            AddrSubnetEither::new(core, prefix).unwrap(),
            fidl_net::Subnet { addr: fidl, prefix_len: prefix },
        )
    }

    #[test]
    fn addr_v4() {
        let bytes = [192, 168, 0, 1];
        let (core, fidl) = create_addr_v4(bytes);

        assert_eq!(core, fidl.into_core());
        assert_eq!(fidl, core.into_fidl());
    }

    #[test]
    fn addr_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let (core, fidl) = create_addr_v6(bytes);

        assert_eq!(core, fidl.into_core());
        assert_eq!(fidl, core.into_fidl());
    }

    #[test]
    fn addr_subnet_v4() {
        let bytes = [192, 168, 0, 1];
        let prefix = 24;
        let (core, fidl) = create_addr_subnet(create_addr_v4(bytes), prefix);

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn addr_subnet_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let prefix = 64;
        let (core, fidl) = create_addr_subnet(create_addr_v6(bytes), prefix);

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn subnet_v4() {
        let bytes = [192, 168, 0, 0];
        let prefix = 24;
        let (core, fidl) = create_subnet(create_addr_v4(bytes), prefix);

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn subnet_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        let prefix = 64;
        let (core, fidl) = create_subnet(create_addr_v6(bytes), prefix);

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn ip_address_state() {
        use fnet_interfaces_admin::AddressAssignmentState;
        use netstack3_core::ip::device::IpAddressState;
        assert_eq!(IpAddressState::Unavailable.into_fidl(), AddressAssignmentState::Unavailable);
        assert_eq!(IpAddressState::Tentative.into_fidl(), AddressAssignmentState::Tentative);
        assert_eq!(IpAddressState::Assigned.into_fidl(), AddressAssignmentState::Assigned);
    }

    #[test_case(
        fidl_net::Ipv6SocketAddress {
            address: net_ip_v6!("1:2:3:4::").into_ext(),
            port: 8080,
            zone_index: 1
        },
        SocketAddressError::UnexpectedZone;
        "IPv6 specified unexpected zone")]
    #[test_case(
        fidl_net::Ipv6SocketAddress {
            address: net_ip_v6!("fe80::1").into_ext(),
            port: 8080,
            zone_index: 2
        },
        SocketAddressError::Device(DeviceNotFoundError);
        "IPv6 specified invalid zone")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn sock_addr_into_core_err<A: SockAddr>(addr: A, expected: SocketAddressError)
    where
        (Option<ZonedAddr<A::AddrType, DeviceId>>, u16):
            TryFromFidlWithContext<A, Error = SocketAddressError>,
        <A::AddrType as IpAddress>::Version: IpSockAddrExt<SocketAddress = A>,
        DeviceId: TryFromFidlWithContext<A::Zone, Error = DeviceNotFoundError>,
    {
        let ctx = FakeConversionContext::new();

        let result: Result<(Option<_>, _), _> = addr.try_into_core_with_ctx(&ctx);
        assert_eq!(result.expect_err("should fail"), expected);
    }

    /// Placeholder for an ID that should be replaced with the real `DeviceId`
    /// from the `FakeConversionContext`.
    struct ReplaceWithCoreId;

    #[test_case(
        fidl_net::Ipv4SocketAddress {address: net_ip_v4!("192.168.0.0").into_ext(), port: 8080},
        (Some(SpecifiedAddr::new(net_ip_v4!("192.168.0.0")).unwrap().into()), 8080);
        "IPv4 specified")]
    #[test_case(
        fidl_net::Ipv4SocketAddress {address: net_ip_v4!("0.0.0.0").into_ext(), port: 8000},
        (None, 8000);
        "IPv4 unspecified")]
    #[test_case(
        fidl_net::Ipv6SocketAddress {
            address: net_ip_v6!("1:2:3:4::").into_ext(),
            port: 8080,
            zone_index: 0
        },
        (Some(SpecifiedAddr::new(net_ip_v6!("1:2:3:4::")).unwrap().into()), 8080);
        "IPv6 specified no zone")]
    #[test_case(
        fidl_net::Ipv6SocketAddress {
            address: net_ip_v6!("::").into_ext(),
            port: 8080,
            zone_index: 0,
        },
        (None, 8080);
        "IPv6 unspecified")]
    #[test_case(
        fidl_net::Ipv6SocketAddress {
            address: net_ip_v6!("fe80::1").into_ext(),
            port: 8080,
            zone_index: 1
        },
        (Some(
            AddrAndZone::new(net_ip_v6!("fe80::1"), ReplaceWithCoreId).unwrap().into()
        ), 8080);
        "IPv6 specified valid zone")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn sock_addr_conversion_reversible<A: SockAddr + Eq + Clone>(
        addr: A,
        (zoned, port): (Option<ZonedAddr<A::AddrType, ReplaceWithCoreId>>, u16),
    ) where
        (Option<ZonedAddr<A::AddrType, DeviceId>>, u16): TryFromFidlWithContext<A, Error = SocketAddressError>
            + TryIntoFidlWithContext<A, Error = DeviceNotFoundError>,
        <A::AddrType as IpAddress>::Version: IpSockAddrExt<SocketAddress = A>,
        DeviceId: TryFromFidlWithContext<A::Zone, Error = DeviceNotFoundError>,
    {
        let ctx = FakeConversionContext::new();
        let zoned = zoned.map(|z| match z {
            ZonedAddr::Unzoned(z) => z.into(),
            ZonedAddr::Zoned(z) => z.map_zone(|ReplaceWithCoreId| ctx.core).into(),
        });

        let result: (Option<ZonedAddr<_, _>>, _) =
            addr.clone().try_into_core_with_ctx(&ctx).expect("into core should succeed");
        assert_eq!(result, (zoned, port));

        let result = result.try_into_fidl_with_ctx(&ctx).expect("reverse should succeed");
        assert_eq!(result, addr)
    }

    #[test]
    fn test_unzoned_ip_port_into_fidl() {
        let ip = net_ip_v4!("1.7.2.4");
        let port = 3893;
        assert_eq!(
            (SpecifiedAddr::new(ip), NonZeroU16::new(port).unwrap()).into_fidl(),
            fidl_net::Ipv4SocketAddress { address: ip.into_ext(), port }
        );

        let ip = net_ip_v6!("1:2:3:4:5::");
        assert_eq!(
            (SpecifiedAddr::new(ip), NonZeroU16::new(port).unwrap()).into_fidl(),
            fidl_net::Ipv6SocketAddress { address: ip.into_ext(), port, zone_index: 0 }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn zoned_addr_port_into_fidl_err() {
        let ctx = FakeConversionContext::new();
        let zoned = ZonedAddr::Zoned(
            AddrAndZone::<Ipv6Addr, _>::new(net_ip_v6!("fe80::1"), ctx.invalid_core).unwrap(),
        );

        let result = (Some(zoned), 9000).try_into_fidl_with_ctx(&ctx);
        assert_eq!(result.expect_err("should fail"), DeviceNotFoundError);
    }

    #[test]
    fn optional_u8_conversion() {
        let empty = fposix_socket::OptionalUint8::Unset(fposix_socket::Empty);
        let empty_core: Option<u8> = empty.into_core();
        assert_eq!(empty_core, None);
        assert_eq!(empty_core.into_fidl(), empty);

        let value = fposix_socket::OptionalUint8::Value(46);
        let value_core: Option<u8> = value.into_core();
        assert_eq!(value_core, Some(46));
        assert_eq!(value_core.into_fidl(), value);
    }

    #[test]
    fn fidl_err_into_warn_and_error() {
        assert_eq!(
            fidl_err_log_level(&fidl::Error::ClientChannelClosed {
                status: zx_status::Status::PEER_CLOSED,
                protocol_name: "PLACEHOLDER_PROTOCOL"
            }),
            log::Level::Warn
        );
        assert_eq!(fidl_err_log_level(&fidl::Error::UnexpectedSyncResponse), log::Level::Error);
    }
}
