// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_stack as fidl_net_stack;
use netstack3_core::{AddrSubnetEither, IpAddr, Ipv4Addr, Ipv6Addr, SubnetEither};
use never::Never;
use std::convert::TryFrom;

/// Error returned when trying create `core` subnets with invalid values.
#[derive(Debug)]
pub struct InvalidSubnetError;

/// Defines a type that can be converted to a FIDL type `F`.
///
/// This trait provides easy conversion to and from FIDL types. All implementers
/// of this trait for `F` automatically provide the inverse conversion for `F`
/// by implementing [`CoreCompatible`] for `F`.
///
/// This trait is meant only to be implemented for types in `netstack3_core`.
pub trait FidlCompatible<F>: Sized {
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
pub trait CoreCompatible<C>: Sized {
    type FromError;
    type IntoError;

    fn try_from_core(core: C) -> Result<Self, Self::FromError>;
    fn try_into_core(self) -> Result<C, Self::IntoError>;
}

/// Utility trait for infallible FIDL conversion.
pub trait FromFidlExt<F>: FidlCompatible<F, FromError = Never> {
    fn from_fidl(fidl: F) -> Self {
        match Self::try_from_fidl(fidl) {
            Ok(slf) => slf,
            Err(err) => match err {},
        }
    }
}

/// Utility trait for infallible FIDL conversion.
pub trait IntoFidlExt<F>: FidlCompatible<F, IntoError = Never> {
    fn into_fidl(self) -> F {
        match self.try_into_fidl() {
            Ok(fidl) => fidl,
            Err(err) => match err {},
        }
    }
}

/// Utility trait for infallible Core conversion.
pub trait FromCoreExt<C>: CoreCompatible<C, FromError = Never> {
    fn from_core(core: C) -> Self {
        match Self::try_from_core(core) {
            Ok(slf) => slf,
            Err(err) => match err {},
        }
    }
}

/// Utility trait for infallible Core conversion.
pub trait IntoCoreExt<C>: CoreCompatible<C, IntoError = Never> {
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

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_net as fidl_net;
    use fidl_fuchsia_net_stack as fidl_net_stack;
    use std::convert::TryFrom;

    #[test]
    fn test_addr_v4() {
        let bytes = [192, 168, 0, 1];
        let core = IpAddr::V4(Ipv4Addr::from(bytes));
        let fidl = fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: bytes });

        assert_eq!(core, fidl.into_core());
        assert_eq!(fidl, core.into_fidl());
    }

    #[test]
    fn test_addr_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let core = IpAddr::V6(Ipv6Addr::from(bytes));
        let fidl = fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address { addr: bytes });

        assert_eq!(core, fidl.into_core());
        assert_eq!(fidl, core.into_fidl());
    }

    #[test]
    fn test_addr_subnet_v4() {
        let bytes = [192, 168, 0, 1];
        let prefix = 24;
        let core = AddrSubnetEither::new(IpAddr::V4(Ipv4Addr::from(bytes)), prefix).unwrap();
        let fidl = fidl_net_stack::InterfaceAddress {
            ip_address: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: bytes }),
            prefix_len: prefix,
        };

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn test_addr_subnet_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        let prefix = 64;
        let core = AddrSubnetEither::new(IpAddr::V6(Ipv6Addr::from(bytes)), prefix).unwrap();
        let fidl = fidl_net_stack::InterfaceAddress {
            ip_address: fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address { addr: bytes }),
            prefix_len: prefix,
        };

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn test_subnet_v4() {
        let bytes = [192, 168, 0, 0];
        let prefix = 24;
        let core = SubnetEither::new(IpAddr::V4(Ipv4Addr::from(bytes)), prefix).unwrap();
        let fidl = fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: bytes }),
            prefix_len: prefix,
        };

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }

    #[test]
    fn test_subnet_v6() {
        let bytes = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        let prefix = 64;
        let core = SubnetEither::new(IpAddr::V6(Ipv6Addr::from(bytes)), prefix).unwrap();
        let fidl = fidl_net::Subnet {
            addr: fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address { addr: bytes }),
            prefix_len: prefix,
        };

        assert_eq!(fidl, core.into_fidl());
        assert_eq!(core, fidl.try_into_core().unwrap());
    }
}
