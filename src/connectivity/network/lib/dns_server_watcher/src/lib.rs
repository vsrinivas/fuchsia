// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! DNS Server watcher.

mod stream;
#[cfg(test)]
mod test_util;

use std::cmp::Ordering;
use std::collections::HashSet;

use fidl_fuchsia_net_name::{
    DhcpDnsServerSource, Dhcpv6DnsServerSource, DnsServerSource, DnsServer_, NdpDnsServerSource,
    StaticDnsServerSource,
};
use fidl_fuchsia_net_name_ext::CloneExt as _;

pub use self::stream::*;

/// The default DNS server port.
pub const DEFAULT_DNS_PORT: u16 = 53;

/// The DNS servers learned from all sources.
#[derive(Default)]
pub struct DnsServers {
    /// DNS servers obtained from the netstack.
    netstack: Vec<DnsServer_>,
}

impl DnsServers {
    /// Sets the DNS servers discovered from `source`.
    pub fn set_servers_from_source(
        &mut self,
        source: DnsServersUpdateSource,
        servers: Vec<DnsServer_>,
    ) {
        match source {
            DnsServersUpdateSource::Netstack => self.netstack = servers,
        }
    }

    /// Returns a consolidated list of servers.
    ///
    /// The servers will be returned deduplicated by their address and sorted by the source
    /// that each server was learned from. The servers will be sorted in most to least
    /// preferred order, with the most preferred server first. The preference of the servers
    /// is NDP, DHCPv4, DHCPv6 then Static, where NDP is the most preferred.
    ///
    /// Note, if multiple `DnsServer_`s have the same address but different sources, only
    /// the `DnsServer_` with the most preferred source will be present in the consolidated
    /// list of servers.
    pub fn consolidated(&self) -> Vec<DnsServer_> {
        let Self { netstack } = self;
        let mut servers = netstack.clone();
        // Sorting happens before deduplication to ensure that when multiple sources report the same
        // address, the highest priority source wins.
        let () = servers.sort_by(Self::ordering);
        let mut addresses = HashSet::new();
        let () = servers.retain(move |s| addresses.insert(s.address));
        servers
    }

    /// Returns the ordering of [`DnsServer_`]s.
    ///
    /// The ordering in greatest to least order is NDP, DHCPv4, DHCPv6 then Static.
    /// An unspecified source will be treated as a static address.
    fn ordering(a: &DnsServer_, b: &DnsServer_) -> Ordering {
        let ordering = |source| match source {
            Some(&DnsServerSource::Ndp(NdpDnsServerSource { source_interface: _ })) => 0,
            Some(&DnsServerSource::Dhcp(DhcpDnsServerSource { source_interface: _ })) => 1,
            Some(&DnsServerSource::Dhcpv6(Dhcpv6DnsServerSource { source_interface: _ })) => 2,
            Some(&DnsServerSource::StaticSource(StaticDnsServerSource {})) | None => 3,
        };
        let a = ordering(a.source.as_ref());
        let b = ordering(b.source.as_ref());
        std::cmp::Ord::cmp(&a, &b)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::constants::*;

    #[test]
    fn test_dnsservers_consolidation() {
        // Simple deduplication and sorting of repeated `DnsServer_`.
        let servers = DnsServers {
            netstack: vec![
                DHCP_SERVER,
                DHCPV6_SERVER,
                NDP_SERVER,
                STATIC_SERVER,
                NDP_SERVER,
                DHCPV6_SERVER,
                DHCP_SERVER,
                STATIC_SERVER,
            ],
        };
        assert_eq!(
            servers.consolidated(),
            vec![NDP_SERVER, DHCP_SERVER, DHCPV6_SERVER, STATIC_SERVER]
        );

        // Deduplication and sorting of same address across different sources.

        // DHCPv6 is not as preferred as NDP so this should not be in the consolidated
        // list.
        let dhcpv6_with_ndp_address = || DnsServer_ {
            source: Some(DnsServerSource::Dhcpv6(Dhcpv6DnsServerSource {
                source_interface: Some(3),
            })),
            ..NDP_SERVER
        };

        let servers = DnsServers {
            netstack: vec![
                dhcpv6_with_ndp_address(),
                DHCP_SERVER,
                DHCPV6_SERVER,
                NDP_SERVER,
                STATIC_SERVER,
            ],
        };
        let expected = vec![NDP_SERVER, DHCP_SERVER, DHCPV6_SERVER, STATIC_SERVER];
        assert_eq!(servers.consolidated(), expected);
        let servers = DnsServers {
            netstack: vec![
                DHCP_SERVER,
                DHCPV6_SERVER,
                NDP_SERVER,
                STATIC_SERVER,
                dhcpv6_with_ndp_address(),
            ],
        };
        assert_eq!(servers.consolidated(), expected);

        // NDP is more preferred than DHCPv6 so `DHCPV6_SERVER` should not be in the consolidated
        // list.
        let ndp_with_dhcpv6_address = || DnsServer_ {
            source: Some(DnsServerSource::Ndp(NdpDnsServerSource { source_interface: Some(3) })),
            ..DHCPV6_SERVER
        };
        let servers = DnsServers {
            netstack: vec![ndp_with_dhcpv6_address(), DHCP_SERVER, DHCPV6_SERVER, STATIC_SERVER],
        };
        let expected = vec![ndp_with_dhcpv6_address(), DHCP_SERVER, STATIC_SERVER];
        assert_eq!(servers.consolidated(), expected);
        let servers = DnsServers {
            netstack: vec![DHCP_SERVER, DHCPV6_SERVER, STATIC_SERVER, ndp_with_dhcpv6_address()],
        };
        assert_eq!(servers.consolidated(), expected);
    }

    #[test]
    fn test_dns_servers_ordering() {
        assert_eq!(DnsServers::ordering(&NDP_SERVER, &NDP_SERVER), Ordering::Equal);
        assert_eq!(DnsServers::ordering(&DHCP_SERVER, &DHCP_SERVER), Ordering::Equal);
        assert_eq!(DnsServers::ordering(&DHCPV6_SERVER, &DHCPV6_SERVER), Ordering::Equal);
        assert_eq!(DnsServers::ordering(&STATIC_SERVER, &STATIC_SERVER), Ordering::Equal);
        assert_eq!(
            DnsServers::ordering(&UNSPECIFIED_SOURCE_SERVER, &UNSPECIFIED_SOURCE_SERVER),
            Ordering::Equal
        );
        assert_eq!(
            DnsServers::ordering(&STATIC_SERVER, &UNSPECIFIED_SOURCE_SERVER),
            Ordering::Equal
        );

        let servers =
            [NDP_SERVER, DHCP_SERVER, DHCPV6_SERVER, STATIC_SERVER, UNSPECIFIED_SOURCE_SERVER];
        // We don't compare the last two servers in the list because their ordering is equal
        // w.r.t. eachother.
        for (i, a) in servers[..servers.len() - 2].iter().enumerate() {
            for b in servers[i + 1..].iter() {
                assert_eq!(DnsServers::ordering(a, b), Ordering::Less);
            }
        }

        let mut servers = vec![DHCPV6_SERVER, DHCP_SERVER, STATIC_SERVER, NDP_SERVER];
        servers.sort_by(DnsServers::ordering);
        assert_eq!(servers, vec![NDP_SERVER, DHCP_SERVER, DHCPV6_SERVER, STATIC_SERVER]);
    }
}
