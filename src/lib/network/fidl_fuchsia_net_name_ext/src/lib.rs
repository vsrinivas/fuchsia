// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for types in the `fidl_fuchsia_net_name` crate.

#![deny(missing_docs, unreachable_patterns)]

use fidl_fuchsia_net_name as fname;

/// A manual implementation of `From`.
pub trait FromExt<T> {
    /// Performs the conversion.
    fn from_ext(f: T) -> Self;
}

/// A manual implementation of `Into`.
///
/// A blanket implementation is provided for implementers of `FromExt<T>`.
pub trait IntoExt<T> {
    /// Performs the conversion.
    fn into_ext(self) -> T;
}

impl<T, U> IntoExt<U> for T
where
    U: FromExt<T>,
{
    fn into_ext(self) -> U {
        U::from_ext(self)
    }
}

impl FromExt<fname::StaticDnsServerSource> for fname::DnsServerSource {
    fn from_ext(f: fname::StaticDnsServerSource) -> fname::DnsServerSource {
        fname::DnsServerSource::StaticSource(f)
    }
}

impl FromExt<fname::DhcpDnsServerSource> for fname::DnsServerSource {
    fn from_ext(f: fname::DhcpDnsServerSource) -> fname::DnsServerSource {
        fname::DnsServerSource::Dhcp(f)
    }
}

impl FromExt<fname::NdpDnsServerSource> for fname::DnsServerSource {
    fn from_ext(f: fname::NdpDnsServerSource) -> fname::DnsServerSource {
        fname::DnsServerSource::Ndp(f)
    }
}

impl FromExt<fname::Dhcpv6DnsServerSource> for fname::DnsServerSource {
    fn from_ext(f: fname::Dhcpv6DnsServerSource) -> fname::DnsServerSource {
        fname::DnsServerSource::Dhcpv6(f)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_from_into_ext() {
        let a = fname::StaticDnsServerSource::empty();
        assert_eq!(fname::DnsServerSource::StaticSource(a.clone()), a.into_ext());

        let a = fname::DhcpDnsServerSource {
            source_interface: Some(1),
            ..fname::DhcpDnsServerSource::empty()
        };
        assert_eq!(fname::DnsServerSource::Dhcp(a.clone()), a.into_ext());

        let a = fname::NdpDnsServerSource {
            source_interface: Some(1),
            ..fname::NdpDnsServerSource::empty()
        };
        assert_eq!(fname::DnsServerSource::Ndp(a.clone()), a.into_ext());

        let a = fname::Dhcpv6DnsServerSource {
            source_interface: Some(1),
            ..fname::Dhcpv6DnsServerSource::empty()
        };
        assert_eq!(fname::DnsServerSource::Dhcpv6(a.clone()), a.into_ext());
    }
}
