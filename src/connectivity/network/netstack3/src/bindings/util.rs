// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_stack as fidl_net_stack;
use net_types::ip::{AddrSubnetEither, IpAddr, SubnetEither};
use net_types::{SpecifiedAddr, Witness};
use netstack3_core::{DeviceId, EntryDest, EntryDestEither, EntryEither, NetstackError};
use never::Never;

/// Error returned when trying to create `core` subnets with invalid values.
#[derive(Debug)]
pub struct InvalidSubnetError;

impl From<InvalidSubnetError> for fidl_net_stack::Error {
    fn from(_error: InvalidSubnetError) -> Self {
        Self::InvalidArgs
    }
}

/// Defines a type that can be converted to a FIDL type `F`.
///
/// This trait provides easy conversion to and from FIDL types. All implementers
/// of this trait for `F` automatically provide the inverse conversion for `F`
/// by implementing [`CoreCompatible`] for `F`.
///
/// This trait is meant only to be implemented for types in `netstack3_core`.
pub(crate) trait FidlCompatible<F>: Sized {
    type FromError;
    type IntoError;

    fn try_from_fidl(fidl: F) -> Result<Self, Self::FromError>;
    fn try_into_fidl(self) -> Result<F, Self::IntoError>;
}

/// Defines a type that can be converted to a Core type `C`.
///
/// This trait offers the convenient symmetrical for [`FidlCompatible`] and is
/// automatically implemented for all types for which a [`FidlCompatible`]
/// conversion is implemented.
pub(crate) trait CoreCompatible<C>: Sized {
    type FromError;
    type IntoError;

    fn try_from_core(core: C) -> Result<Self, Self::FromError>;
    fn try_into_core(self) -> Result<C, Self::IntoError>;
}

/// Utility trait for infallible FIDL conversion.
pub(crate) trait FromFidlExt<F>: FidlCompatible<F, FromError = Never> {
    fn from_fidl(fidl: F) -> Self {
        match Self::try_from_fidl(fidl) {
            Ok(slf) => slf,
            Err(err) => match err {},
        }
    }
}

/// Utility trait for infallible FIDL conversion.
pub(crate) trait IntoFidlExt<F>: FidlCompatible<F, IntoError = Never> {
    fn into_fidl(self) -> F {
        match self.try_into_fidl() {
            Ok(fidl) => fidl,
            Err(err) => match err {},
        }
    }
}

/// Utility trait for infallible Core conversion.
pub(crate) trait FromCoreExt<C>: CoreCompatible<C, FromError = Never> {
    fn from_core(core: C) -> Self {
        match Self::try_from_core(core) {
            Ok(slf) => slf,
            Err(err) => match err {},
        }
    }
}

/// Utility trait for infallible Core conversion.
pub(crate) trait IntoCoreExt<C>: CoreCompatible<C, IntoError = Never> {
    fn into_core(self) -> C {
        match self.try_into_core() {
            Ok(core) => core,
            Err(err) => match err {},
        }
    }
}

impl<F, C> CoreCompatible<C> for F
where
    C: FidlCompatible<F>,
{
    type FromError = C::IntoError;
    type IntoError = C::FromError;

    fn try_from_core(core: C) -> Result<Self, Self::FromError> {
        core.try_into_fidl()
    }

    fn try_into_core(self) -> Result<C, Self::IntoError> {
        C::try_from_fidl(self)
    }
}

impl<C, F: CoreCompatible<C, IntoError = Never>> IntoCoreExt<C> for F {}
impl<C, F: CoreCompatible<C, FromError = Never>> FromCoreExt<C> for F {}
impl<F, C: FidlCompatible<F, IntoError = Never>> IntoFidlExt<F> for C {}
impl<F, C: FidlCompatible<F, FromError = Never>> FromFidlExt<F> for C {}

// NOTE This error exists solely to support implementing
// `FidlCompatible<fidl_net_stack::Error>` for `NetstackError` so that the
// latter can be converted into the former. There is no real use case for
// converting in the other direction, so realistically this error should never
// be seen.
/// Error indicating that a value cannot be converted into another type because
/// there is no appropriate representation of the value in the target type.
#[derive(Debug)]
pub struct NotRepresentedError;

impl FidlCompatible<fidl_net_stack::Error> for NetstackError {
    type FromError = NotRepresentedError;
    type IntoError = Never;

    fn try_from_fidl(fidl: fidl_net_stack::Error) -> Result<Self, Self::FromError> {
        match fidl {
            fidl_net_stack::Error::AlreadyExists => Ok(NetstackError::Exists),
            fidl_net_stack::Error::NotFound => Ok(NetstackError::NotFound),
            _ => Err(NotRepresentedError),
        }
    }

    fn try_into_fidl(self) -> Result<fidl_net_stack::Error, Self::IntoError> {
        match self {
            NetstackError::Exists => Ok(fidl_net_stack::Error::AlreadyExists),
            NetstackError::NotFound => Ok(fidl_net_stack::Error::NotFound),
            _ => Ok(fidl_net_stack::Error::Internal),
        }
    }
}

impl FidlCompatible<fidl_net::IpAddress> for IpAddr {
    type FromError = Never;
    type IntoError = Never;

    fn try_from_fidl(fidl: fidl_net::IpAddress) -> Result<Self, Self::FromError> {
        match fidl {
            fidl_net::IpAddress::Ipv4(v4) => Ok(IpAddr::V4(v4.addr.into())),
            fidl_net::IpAddress::Ipv6(v6) => Ok(IpAddr::V6(v6.addr.into())),
        }
    }

    fn try_into_fidl(self) -> Result<fidl_net::IpAddress, Self::IntoError> {
        match self {
            IpAddr::V4(addr) => {
                Ok(fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: addr.ipv4_bytes() }))
            }
            IpAddr::V6(addr) => {
                Ok(fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address { addr: addr.ipv6_bytes() }))
            }
        }
    }
}

/// An error indicating that an address was a member of the wrong class (for
/// example, a unicast address used where a multicast address is required).
pub struct AddrClassError;

// TODO(joshlf): Introduce a separate variant to `fidl_net_stack::Error` for
// `AddrClassError`?
impl From<AddrClassError> for fidl_net_stack::Error {
    fn from(_err: AddrClassError) -> Self {
        fidl_net_stack::Error::InvalidArgs
    }
}

impl FidlCompatible<fidl_net::IpAddress> for SpecifiedAddr<IpAddr> {
    type FromError = AddrClassError;
    type IntoError = Never;

    fn try_from_fidl(fidl: fidl_net::IpAddress) -> Result<Self, AddrClassError> {
        SpecifiedAddr::new(fidl.into_core()).ok_or(AddrClassError)
    }

    fn try_into_fidl(self) -> Result<fidl_net::IpAddress, Never> {
        Ok(self.into_addr().into_fidl())
    }
}

impl FidlCompatible<fidl_net_stack::InterfaceAddress> for AddrSubnetEither {
    type FromError = InvalidSubnetError;
    type IntoError = Never;

    fn try_from_fidl(fidl: fidl_net_stack::InterfaceAddress) -> Result<Self, Self::FromError> {
        AddrSubnetEither::new(fidl.ip_address.into_core(), fidl.prefix_len)
            .ok_or(InvalidSubnetError)
    }

    fn try_into_fidl(self) -> Result<fidl_net_stack::InterfaceAddress, Self::IntoError> {
        let (addr, prefix) = self.into_addr_prefix();
        Ok(fidl_net_stack::InterfaceAddress { ip_address: addr.into_fidl(), prefix_len: prefix })
    }
}

impl FidlCompatible<fidl_net::Subnet> for SubnetEither {
    type FromError = InvalidSubnetError;
    type IntoError = Never;

    fn try_from_fidl(fidl: fidl_net::Subnet) -> Result<Self, Self::FromError> {
        SubnetEither::new(fidl.addr.into_core(), fidl.prefix_len).ok_or(InvalidSubnetError)
    }

    fn try_into_fidl(self) -> Result<fidl_net::Subnet, Self::IntoError> {
        let (net, prefix) = self.into_net_prefix();
        Ok(fidl_net::Subnet { addr: net.into_fidl(), prefix_len: prefix })
    }
}

/// Provides a stateful context for operations that require state-keeping to be
/// completed.
///
/// `ConversionContext` is used by conversion functions in
/// [`ContextFidlCompatible`] and [`ContextCoreCompatible`].
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

/// Defines a type that can be converted to a FIDL type `F`, given a context
/// that implements [`ConversionContext`].
///
/// This trait provides easy conversion to and from FIDL types. All implementers
/// of this trait for `F` automatically provide the inverse conversion for `F`
/// by implementing [`ContextCoreCompatible`] for `F`.
///
/// This trait is meant only to be implemented for types in `netstack3_core`.
pub(crate) trait ContextFidlCompatible<F>: Sized {
    type FromError;
    type IntoError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: F,
    ) -> Result<Self, Self::FromError>;
    fn try_into_fidl_with_ctx<C: ConversionContext>(self, ctx: &C) -> Result<F, Self::IntoError>;
}

/// Defines a type that can be converted to a Core type `C`, given a context
/// that implements [`ConversionContext`].
///
/// This trait offers the convenient symmetrical for [`ContextFidlCompatible`]
/// and is automatically implemented for all types for which a
/// [`ContextFidlCompatible`] conversion is implemented.
pub(crate) trait ContextCoreCompatible<T>: Sized {
    type FromError;
    type IntoError;

    fn try_from_core_with_ctx<C: ConversionContext>(
        ctx: &C,
        core: T,
    ) -> Result<Self, Self::FromError>;
    fn try_into_core_with_ctx<C: ConversionContext>(self, ctx: &C) -> Result<T, Self::IntoError>;
}

impl<F, C> ContextCoreCompatible<C> for F
where
    C: ContextFidlCompatible<F>,
{
    type FromError = C::IntoError;
    type IntoError = C::FromError;

    fn try_from_core_with_ctx<X: ConversionContext>(
        ctx: &X,
        core: C,
    ) -> Result<Self, Self::FromError> {
        core.try_into_fidl_with_ctx(ctx)
    }

    fn try_into_core_with_ctx<X: ConversionContext>(self, ctx: &X) -> Result<C, Self::IntoError> {
        C::try_from_fidl_with_ctx(ctx, self)
    }
}

#[derive(Debug)]
pub struct DeviceNotFoundError;

impl ContextFidlCompatible<u64> for DeviceId {
    type FromError = DeviceNotFoundError;
    type IntoError = DeviceNotFoundError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: u64,
    ) -> Result<Self, Self::FromError> {
        ctx.get_core_id(fidl).ok_or(DeviceNotFoundError)
    }

    fn try_into_fidl_with_ctx<C: ConversionContext>(self, ctx: &C) -> Result<u64, Self::IntoError> {
        ctx.get_binding_id(self).ok_or(DeviceNotFoundError)
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum ForwardingConversionError {
    DeviceNotFound,
    TypeMismatch,
    InvalidSubnet,
    AddrClassError,
}

impl From<DeviceNotFoundError> for ForwardingConversionError {
    fn from(_: DeviceNotFoundError) -> Self {
        ForwardingConversionError::DeviceNotFound
    }
}

impl From<InvalidSubnetError> for ForwardingConversionError {
    fn from(_: InvalidSubnetError) -> Self {
        ForwardingConversionError::InvalidSubnet
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
            | ForwardingConversionError::InvalidSubnet
            | ForwardingConversionError::AddrClassError => fidl_net_stack::Error::InvalidArgs,
        }
    }
}

impl ContextFidlCompatible<fidl_net_stack::ForwardingDestination> for EntryDestEither<DeviceId> {
    type FromError = ForwardingConversionError;
    type IntoError = DeviceNotFoundError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: fidl_net_stack::ForwardingDestination,
    ) -> Result<Self, Self::FromError> {
        Ok(match fidl {
            fidl_net_stack::ForwardingDestination::DeviceId(binding_id) => {
                EntryDest::Local { device: binding_id.try_into_core_with_ctx(ctx)? }
            }
            fidl_net_stack::ForwardingDestination::NextHop(addr) => {
                EntryDest::Remote { next_hop: addr.try_into_core()? }
            }
        })
    }

    fn try_into_fidl_with_ctx<C: ConversionContext>(
        self,
        ctx: &C,
    ) -> Result<fidl_net_stack::ForwardingDestination, Self::IntoError> {
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

impl ContextFidlCompatible<fidl_net_stack::ForwardingEntry> for EntryEither<DeviceId> {
    type FromError = ForwardingConversionError;
    type IntoError = DeviceNotFoundError;

    fn try_from_fidl_with_ctx<C: ConversionContext>(
        ctx: &C,
        fidl: fidl_net_stack::ForwardingEntry,
    ) -> Result<Self, Self::FromError> {
        EntryEither::new(
            fidl.subnet.try_into_core()?,
            fidl.destination.try_into_core_with_ctx(ctx)?,
        )
        .ok_or(ForwardingConversionError::TypeMismatch)
    }

    fn try_into_fidl_with_ctx<C: ConversionContext>(
        self,
        ctx: &C,
    ) -> Result<fidl_net_stack::ForwardingEntry, Self::IntoError> {
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
    ) -> (AddrSubnetEither, fidl_net_stack::InterfaceAddress) {
        let (core, fidl) = addr;
        (
            AddrSubnetEither::new(core, prefix).unwrap(),
            fidl_net_stack::InterfaceAddress { ip_address: fidl, prefix_len: prefix },
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
            ForwardingConversionError::InvalidSubnet
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
