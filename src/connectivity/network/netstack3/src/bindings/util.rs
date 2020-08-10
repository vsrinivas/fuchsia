// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_stack as fidl_net_stack;
use net_types::ip::{
    AddrSubnetEither, AddrSubnetError, IpAddr, Ipv4Addr, Ipv6Addr, SubnetEither, SubnetError,
};
use net_types::{SpecifiedAddr, Witness};
use netstack3_core::{DeviceId, EntryDest, EntryDestEither, EntryEither, NetstackError};
use never::Never;

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
            Err(never) => never.into_any(),
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
/// `IntoCore<C>` extends [`TryIntoCore<C>`] where `<C as TryFromFidl<_>>::Error =
/// Never`, and provides the infallible conversion method [`into_core`].
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
            Err(never) => never.into_any(),
        }
    }
}

impl<F, C: TryFromFidl<F, Error = Never>> IntoCore<C> for F {}

impl<T> TryIntoFidl<T> for Never {
    type Error = Never;

    fn try_into_fidl(self) -> Result<T, Never> {
        self.into_any()
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

/// An error indicating that an address was a member of the wrong class (for
/// example, a unicast address used where a multicast address is required).
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
        Ok(self.into_addr().into_fidl())
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
        let (addr, prefix) = self.into_addr_prefix();
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
        let (net, prefix) = self.into_net_prefix();
        Ok(fidl_net::Subnet { addr: net.into_fidl(), prefix_len: prefix })
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

#[derive(Debug)]
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

impl TryIntoFidlWithContext<u64> for DeviceId {
    type Error = DeviceNotFoundError;

    fn try_into_fidl_with_ctx<C: ConversionContext>(
        self,
        ctx: &C,
    ) -> Result<u64, DeviceNotFoundError> {
        ctx.get_binding_id(self).ok_or(DeviceNotFoundError)
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

impl TryFromFidlWithContext<fidl_net_stack::ForwardingDestination> for EntryDestEither<DeviceId> {
    type Error = ForwardingConversionError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: fidl_net_stack::ForwardingDestination,
    ) -> Result<EntryDestEither<DeviceId>, ForwardingConversionError> {
        Ok(match fidl {
            fidl_net_stack::ForwardingDestination::DeviceId(binding_id) => {
                EntryDest::Local { device: binding_id.try_into_core_with_ctx(ctx)? }
            }
            fidl_net_stack::ForwardingDestination::NextHop(addr) => {
                EntryDest::Remote { next_hop: addr.try_into_core()? }
            }
        })
    }
}

impl TryIntoFidlWithContext<fidl_net_stack::ForwardingDestination> for EntryDestEither<DeviceId> {
    type Error = DeviceNotFoundError;

    fn try_into_fidl_with_ctx<C: ConversionContext>(
        self,
        ctx: &C,
    ) -> Result<fidl_net_stack::ForwardingDestination, DeviceNotFoundError> {
        Ok(match self {
            EntryDest::Local { device } => {
                fidl_net_stack::ForwardingDestination::DeviceId(device.try_into_fidl_with_ctx(ctx)?)
            }
            EntryDest::Remote { next_hop } => {
                let next_hop: IpAddr = next_hop.into_addr().into();
                fidl_net_stack::ForwardingDestination::NextHop(next_hop.into_fidl())
            }
        })
    }
}

impl TryFromFidlWithContext<fidl_net_stack::ForwardingEntry> for EntryEither<DeviceId> {
    type Error = ForwardingConversionError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: fidl_net_stack::ForwardingEntry,
    ) -> Result<EntryEither<DeviceId>, ForwardingConversionError> {
        EntryEither::new(
            fidl.subnet.try_into_core()?,
            fidl.destination.try_into_core_with_ctx(ctx)?,
        )
        .ok_or(ForwardingConversionError::TypeMismatch)
    }
}

impl TryIntoFidlWithContext<fidl_net_stack::ForwardingEntry> for EntryEither<DeviceId> {
    type Error = DeviceNotFoundError;

    fn try_into_fidl_with_ctx<C: ConversionContext>(
        self,
        ctx: &C,
    ) -> Result<fidl_net_stack::ForwardingEntry, DeviceNotFoundError> {
        let (subnet, dest) = self.into_subnet_dest();
        Ok(fidl_net_stack::ForwardingEntry {
            subnet: subnet.into_fidl(),
            destination: dest.try_into_fidl_with_ctx(ctx)?,
        })
    }
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_net as fidl_net;
    use fidl_fuchsia_net_stack as fidl_net_stack;
    use net_types::ethernet::Mac;
    use net_types::ip::{Ipv4Addr, Ipv6Addr};

    use super::*;
    use crate::bindings::Netstack;

    struct FakeConversionContext {
        binding: u64,
        core: DeviceId,
    }

    impl FakeConversionContext {
        fn new() -> Self {
            // we need a valid context to be able to create DeviceIds, so
            // we just create it, get the device id and then destroy everything
            let netstack = Netstack::new();
            let core = netstack
                .ctx
                .try_lock()
                .unwrap()
                .state_mut()
                .add_ethernet_device(Mac::new([1, 2, 3, 4, 5, 6]), 1500);
            Self { binding: 1, core }
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

    fn create_local_dest_entry(
        binding: u64,
        core: DeviceId,
    ) -> (EntryDestEither<DeviceId>, fidl_net_stack::ForwardingDestination) {
        let core = EntryDest::<IpAddr, _>::Local { device: core };
        let fidl = fidl_net_stack::ForwardingDestination::DeviceId(binding);
        (core, fidl)
    }

    fn create_remote_dest_entry(
        addr: (IpAddr, fidl_net::IpAddress),
    ) -> (EntryDestEither<DeviceId>, fidl_net_stack::ForwardingDestination) {
        let (core, fidl) = addr;
        let core = EntryDest::<IpAddr, _>::Remote { next_hop: SpecifiedAddr::new(core).unwrap() };
        let fidl = fidl_net_stack::ForwardingDestination::NextHop(fidl);
        (core, fidl)
    }

    fn create_forwarding_entry(
        subnet: (SubnetEither, fidl_net::Subnet),
        dest: (EntryDestEither<DeviceId>, fidl_net_stack::ForwardingDestination),
    ) -> (EntryEither<DeviceId>, fidl_net_stack::ForwardingEntry) {
        let (core_s, fidl_s) = subnet;
        let (core_d, fidl_d) = dest;
        (
            EntryEither::new(core_s, core_d).unwrap(),
            fidl_net_stack::ForwardingEntry { subnet: fidl_s, destination: fidl_d },
        )
    }

    #[test]
    fn test_addr_v4() {
        let bytes = [192, 168, 0, 1];
        let (core, fidl) = create_addr_v4(bytes);

        assert_eq!(core, fidl.into_core());
        assert_eq!(fidl, core.into_fidl());
    }

    #[test]
    fn test_addr_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let (core, fidl) = create_addr_v6(bytes);

        assert_eq!(core, fidl.into_core());
        assert_eq!(fidl, core.into_fidl());
    }

    #[test]
    fn test_addr_subnet_v4() {
        let bytes = [192, 168, 0, 1];
        let prefix = 24;
        let (core, fidl) = create_addr_subnet(create_addr_v4(bytes), prefix);

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn test_addr_subnet_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let prefix = 64;
        let (core, fidl) = create_addr_subnet(create_addr_v6(bytes), prefix);

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn test_subnet_v4() {
        let bytes = [192, 168, 0, 0];
        let prefix = 24;
        let (core, fidl) = create_subnet(create_addr_v4(bytes), prefix);

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn test_subnet_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        let prefix = 64;
        let (core, fidl) = create_subnet(create_addr_v6(bytes), prefix);

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn test_entry_dest_device() {
        let ctx = FakeConversionContext::new();

        let (core, fidl) = create_local_dest_entry(ctx.binding, ctx.core);

        assert_eq!(fidl, core.try_into_fidl_with_ctx(&ctx).unwrap());
        assert_eq!(core, fidl.try_into_core_with_ctx(&ctx).unwrap());

        let (_, fidl) = create_local_dest_entry(ctx.binding, ctx.core);

        assert!(EntryDestEither::try_from_fidl_with_ctx(&EmptyFakeConversionContext, fidl).is_err());
        assert!(fidl_net_stack::ForwardingDestination::try_from_core_with_ctx(
            &EmptyFakeConversionContext,
            core,
        )
        .is_err());
    }

    #[test]
    fn test_entry_dest_v4() {
        let bytes = [192, 168, 0, 1];
        let ctx = EmptyFakeConversionContext;
        let (core, fidl) = create_remote_dest_entry(create_addr_v4(bytes));

        assert_eq!(fidl, core.try_into_fidl_with_ctx(&ctx).unwrap());
        assert_eq!(core, fidl.try_into_core_with_ctx(&ctx).unwrap());
    }

    #[test]
    fn test_entry_dest_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let ctx = EmptyFakeConversionContext;
        let (core, fidl) = create_remote_dest_entry(create_addr_v6(bytes));

        assert_eq!(fidl, core.try_into_fidl_with_ctx(&ctx).unwrap());
        assert_eq!(core, fidl.try_into_core_with_ctx(&ctx).unwrap());
    }

    #[test]
    fn test_forwarding_entry_v4() {
        let dst_bytes = [192, 168, 0, 1];
        let subnet_bytes = [192, 168, 0, 0];
        let prefix = 24;
        let ctx = EmptyFakeConversionContext;
        let (core, fidl) = create_forwarding_entry(
            create_subnet(create_addr_v4(subnet_bytes), prefix),
            create_remote_dest_entry(create_addr_v4(dst_bytes)),
        );

        assert_eq!(fidl, core.try_into_fidl_with_ctx(&ctx).unwrap());
        assert_eq!(core, fidl.try_into_core_with_ctx(&ctx).unwrap());
    }

    #[test]
    fn test_forwarding_entry_v6() {
        let dst_bytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let subnet_bytes = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        let prefix = 64;
        let ctx = EmptyFakeConversionContext;
        let (core, fidl) = create_forwarding_entry(
            create_subnet(create_addr_v6(subnet_bytes), prefix),
            create_remote_dest_entry(create_addr_v6(dst_bytes)),
        );

        assert_eq!(fidl, core.try_into_fidl_with_ctx(&ctx).unwrap());
        assert_eq!(core, fidl.try_into_core_with_ctx(&ctx).unwrap());
    }

    #[test]
    fn test_forwarding_entry_device() {
        let ctx = FakeConversionContext::new();
        let subnet_bytes = [192, 168, 0, 0];
        let prefix = 24;

        let (core, fidl) = create_forwarding_entry(
            create_subnet(create_addr_v4(subnet_bytes), prefix),
            create_local_dest_entry(ctx.binding, ctx.core),
        );

        assert_eq!(fidl, core.try_into_fidl_with_ctx(&ctx).unwrap());
        assert_eq!(core, fidl.try_into_core_with_ctx(&ctx).unwrap());
    }

    #[test]
    fn test_forwarding_entry_errors() {
        let valid_ctx = FakeConversionContext::new();
        let ctx = EmptyFakeConversionContext;
        let subnet_bytes = [192, 168, 0, 0];
        let prefix = 24;
        let bad_addr_bytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];

        let (core, mut fidl) = create_forwarding_entry(
            create_subnet(create_addr_v4(subnet_bytes), prefix),
            create_local_dest_entry(valid_ctx.binding, valid_ctx.core),
        );
        // check device not found works:
        assert!(fidl_net_stack::ForwardingEntry::try_from_core_with_ctx(&ctx, core).is_err());
        assert_eq!(
            EntryEither::try_from_fidl_with_ctx(&ctx, fidl).unwrap_err(),
            ForwardingConversionError::DeviceNotFound
        );

        // try with an invalid subnet (fidl is not Clone, so we keep
        // re-generating it from core):
        fidl = core.try_into_fidl_with_ctx(&valid_ctx).unwrap();
        fidl.subnet.prefix_len = 64;
        assert_eq!(
            EntryEither::try_from_fidl_with_ctx(&ctx, fidl).unwrap_err(),
            ForwardingConversionError::Subnet(SubnetError::PrefixTooLong)
        );

        // Try with a mismatched address:
        fidl = core.try_into_fidl_with_ctx(&valid_ctx).unwrap();
        fidl.destination = create_remote_dest_entry(create_addr_v6(bad_addr_bytes)).1;
        assert_eq!(
            EntryEither::try_from_fidl_with_ctx(&ctx, fidl).unwrap_err(),
            ForwardingConversionError::TypeMismatch
        );
    }
}
