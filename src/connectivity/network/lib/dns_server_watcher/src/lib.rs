// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! DNS Server watcher.

mod stream;
#[cfg(test)]
mod test_util;

use std::cmp::Ordering;
use std::collections::{HashMap, HashSet};

use fidl_fuchsia_net::SocketAddress;
use fidl_fuchsia_net_name::{
    DhcpDnsServerSource, Dhcpv6DnsServerSource, DnsServerSource, DnsServer_, NdpDnsServerSource,
    StaticDnsServerSource,
};
use fidl_fuchsia_net_name_ext::CloneExt;

pub use self::stream::*;

/// The default DNS server port.
pub const DEFAULT_DNS_PORT: u16 = 53;

/// The DNS servers learned from all sources.
#[derive(Default)]
pub struct DnsServers {
    /// DNS servers obtained from some default configurations.
    ///
    /// These servers will have the lowest priority of all servers.
    default: Vec<DnsServer_>,

    /// DNS servers obtained from the netstack.
    netstack: Vec<DnsServer_>,

    /// DNS servers obtained from DHCPv6 clients.
    dhcpv6: HashMap<u64, Vec<DnsServer_>>,
}

impl DnsServers {
    /// Sets the DNS servers discovered from `source`.
    // TODO(fxbug.dev/55672): Make sure `servers` only contain servers that could be obtained
    // from `source`.
    pub fn set_servers_from_source(
        &mut self,
        source: DnsServersUpdateSource,
        servers: Vec<DnsServer_>,
    ) {
        match source {
            DnsServersUpdateSource::Default => self.default = servers,
            DnsServersUpdateSource::Netstack => self.netstack = servers,
            DnsServersUpdateSource::Dhcpv6 { interface_id } => {
                // We discard existing servers since they are being replaced with
                // `servers` - the old servers are useless to us now.
                let _: Option<Vec<DnsServer_>> = if servers.is_empty() {
                    self.dhcpv6.remove(&interface_id)
                } else {
                    self.dhcpv6.insert(interface_id, servers)
                };
            }
        }
    }

    /// Returns a consolidated list of server addresses.
    ///
    /// The servers will be returned deduplicated by their address and sorted by the source
    /// that each server was learned from. The servers will be sorted in most to least
    /// preferred order, with the most preferred server first. The preference of the servers
    /// is NDP, DHCPv4, DHCPv6 then Static, where NDP is the most preferred. No ordering is
    /// guaranteed across servers from different sources of the same source-kind, but ordering
    /// within a source is maintained.
    ///
    /// Example, say we had servers SA1 and SA2 set for DHCPv6 interface A, and SB1 and SB2
    /// for DHCPv6 interface B. The consolidated list will be either one of [SA1, SA2, SB1, SB2]
    /// or [SB1, SB2, SA1, SA2]. Notice how the ordering across sources (DHCPv6 interface A vs
    /// DHCPv6 interface B) is not fixed, but the ordering within the source ([SA1, SA2] for DHCPv6
    /// interface A and [SB1, SB2] for DHCPv6 interface B) is maintained.
    ///
    /// Note, if multiple `DnsServer_`s have the same address but different sources, only
    /// the `DnsServer_` with the most preferred source will be present in the consolidated
    /// list of servers.
    // TODO(fxbug.dev/55893): Consider ordering across sources of the same source-kind based on some
    // metric.
    pub fn consolidated(&self) -> Vec<SocketAddress> {
        self.consolidate_filter_map(|x| x.address)
    }

    /// Returns a consolidated list of servers, with the mapping function `f` applied.
    ///
    /// See `consolidated` for details on ordering.
    fn consolidate_filter_map<T, F: Fn(DnsServer_) -> Option<T>>(&self, f: F) -> Vec<T> {
        let Self { default, netstack, dhcpv6 } = self;
        let mut servers = netstack
            .iter()
            .chain(dhcpv6.values().flatten())
            .map(CloneExt::clone)
            .collect::<Vec<_>>();
        // Sorting happens before deduplication to ensure that when multiple sources report the same
        // address, the highest priority source wins.
        //
        // `sort_by` maintains the order of equal elements. This is required to maintain ordering
        // within a source of DNS servers. `sort_unstable_by` may not preserve this ordering.
        let () = servers.sort_by(Self::ordering);
        // Default servers are considered to have the lowest priority so we append them to the end
        // of the list of sorted dynamically learned servers.
        let () = servers.extend(default.clone());
        let mut addresses = HashSet::new();
        let () = servers.retain(move |s| addresses.insert(s.address));
        servers.into_iter().filter_map(f).collect()
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
    use fidl_fuchsia_net as fnet;

    use super::*;
    use crate::test_util::constants::*;

    /// Assert that `got` and `want` are equal, for the purposes of our tests.
    ///
    /// The elements in `got` and `want` do not need to be in the same order within
    /// `loosly_equal_range` to be considered equal.
    fn loosly_assert_servers(
        got: Vec<fnet::SocketAddress>,
        want: Vec<fnet::SocketAddress>,
        loosly_equal_range: std::ops::Range<usize>,
    ) {
        assert_eq!(got[..loosly_equal_range.start], want[..loosly_equal_range.start]);
        assert_eq!(got[loosly_equal_range.end..], want[loosly_equal_range.end..]);
        assert_eq!(
            got[loosly_equal_range.clone()].iter().collect::<HashSet<_>>(),
            want[loosly_equal_range].iter().collect::<HashSet<_>>()
        );
    }

    #[test]
    fn test_dnsservers_consolidation() {
        // Simple deduplication and sorting of repeated `DnsServer_`.
        let mut dhcpv6 = HashMap::new();
        // `DHCPV6_SERVER2` would normally only come from an interface with ID
        // `DHCPV6_SERVER2_INTERFACE_ID`, but we are just testing deduplication
        // logic here.
        dhcpv6.insert(DHCPV6_SERVER1_INTERFACE_ID, vec![DHCPV6_SERVER1, DHCPV6_SERVER2]);
        dhcpv6.insert(DHCPV6_SERVER2_INTERFACE_ID, vec![DHCPV6_SERVER1, DHCPV6_SERVER2]);
        let servers = DnsServers {
            default: vec![NDP_SERVER, NDP_SERVER],
            netstack: vec![
                DHCP_SERVER,
                NDP_SERVER,
                STATIC_SERVER,
                NDP_SERVER,
                DHCP_SERVER,
                STATIC_SERVER,
            ],
            dhcpv6,
        };
        // Ordering across (the DHCPv6) sources is not guaranteed, but both DHCPv6 sources
        // have the same set of servers with the same order. With deduplication, we know
        // we will only see one of the sources' servers.
        assert_eq!(
            servers.consolidated(),
            vec![
                NDP_SOURCE_SOCKADDR,
                DHCP_SOURCE_SOCKADDR,
                DHCPV6_SOURCE_SOCKADDR1,
                DHCPV6_SOURCE_SOCKADDR2,
                STATIC_SOURCE_SOCKADDR,
            ],
        );

        // Default servers should always have low priority, but if the same server
        // is observed by a higher priority source, then use the higher source for
        // ordering.
        let mut dhcpv6 = HashMap::new();
        dhcpv6.insert(DHCPV6_SERVER1_INTERFACE_ID, vec![DHCPV6_SERVER1]);
        dhcpv6.insert(DHCPV6_SERVER2_INTERFACE_ID, vec![DHCPV6_SERVER2]);
        let servers = DnsServers {
            default: vec![NDP_SERVER, NDP_SERVER, DHCP_SERVER],
            netstack: vec![DHCP_SERVER, STATIC_SERVER, DHCP_SERVER, STATIC_SERVER],
            dhcpv6,
        };
        // No ordering is guaranteed across servers from different sources of the same
        // source-kind.
        let () = loosly_assert_servers(
            servers.consolidated(),
            vec![
                DHCP_SOURCE_SOCKADDR,
                DHCPV6_SOURCE_SOCKADDR1,
                DHCPV6_SOURCE_SOCKADDR2,
                STATIC_SOURCE_SOCKADDR,
                NDP_SOURCE_SOCKADDR,
            ],
            1..3,
        );

        // Deduplication and sorting of same address across different sources.

        // DHCPv6 is not as preferred as NDP so this should not be in the consolidated
        // servers list.
        let dhcpv6_with_ndp_address = || DnsServer_ {
            address: Some(NDP_SOURCE_SOCKADDR),
            source: Some(DnsServerSource::Dhcpv6(Dhcpv6DnsServerSource {
                source_interface: Some(DHCPV6_SERVER1_INTERFACE_ID),
            })),
        };
        let mut dhcpv6 = HashMap::new();
        dhcpv6.insert(DHCPV6_SERVER1_INTERFACE_ID, vec![dhcpv6_with_ndp_address(), DHCPV6_SERVER1]);
        let mut servers = DnsServers {
            default: vec![],
            netstack: vec![dhcpv6_with_ndp_address(), DHCP_SERVER, NDP_SERVER, STATIC_SERVER],
            dhcpv6,
        };
        let expected_servers = vec![NDP_SERVER, DHCP_SERVER, DHCPV6_SERVER1, STATIC_SERVER];
        assert_eq!(servers.consolidate_filter_map(Some), expected_servers);
        let expected_sockaddrs = vec![
            NDP_SOURCE_SOCKADDR,
            DHCP_SOURCE_SOCKADDR,
            DHCPV6_SOURCE_SOCKADDR1,
            STATIC_SOURCE_SOCKADDR,
        ];
        assert_eq!(servers.consolidated(), expected_sockaddrs);
        servers.netstack = vec![DHCP_SERVER, NDP_SERVER, STATIC_SERVER, dhcpv6_with_ndp_address()];
        assert_eq!(servers.consolidate_filter_map(Some), expected_servers);
        assert_eq!(servers.consolidated(), expected_sockaddrs);

        // NDP is more preferred than DHCPv6 so `DHCPV6_SERVER1` should not be in the
        // consolidated list of servers.
        let ndp_with_dhcpv6_sockaddr1 = || DnsServer_ {
            address: Some(DHCPV6_SOURCE_SOCKADDR1),
            source: Some(DnsServerSource::Ndp(NdpDnsServerSource {
                source_interface: Some(NDP_SERVER_INTERFACE_ID),
            })),
        };

        let mut dhcpv6 = HashMap::new();
        dhcpv6.insert(DHCPV6_SERVER1_INTERFACE_ID, vec![DHCPV6_SERVER1]);
        dhcpv6.insert(DHCPV6_SERVER2_INTERFACE_ID, vec![DHCPV6_SERVER2]);
        let mut servers = DnsServers {
            default: vec![],
            netstack: vec![ndp_with_dhcpv6_sockaddr1(), DHCP_SERVER, STATIC_SERVER],
            dhcpv6,
        };
        let expected_servers =
            vec![ndp_with_dhcpv6_sockaddr1(), DHCP_SERVER, DHCPV6_SERVER2, STATIC_SERVER];
        assert_eq!(servers.consolidate_filter_map(Some), expected_servers);
        let expected_sockaddrs = vec![
            DHCPV6_SOURCE_SOCKADDR1,
            DHCP_SOURCE_SOCKADDR,
            DHCPV6_SOURCE_SOCKADDR2,
            STATIC_SOURCE_SOCKADDR,
        ];
        assert_eq!(servers.consolidated(), expected_sockaddrs);
        servers.netstack = vec![DHCP_SERVER, STATIC_SERVER, ndp_with_dhcpv6_sockaddr1()];
        assert_eq!(servers.consolidate_filter_map(Some), expected_servers);
        assert_eq!(servers.consolidated(), expected_sockaddrs);
    }

    #[test]
    fn test_dns_servers_ordering() {
        assert_eq!(DnsServers::ordering(&NDP_SERVER, &NDP_SERVER), Ordering::Equal);
        assert_eq!(DnsServers::ordering(&DHCP_SERVER, &DHCP_SERVER), Ordering::Equal);
        assert_eq!(DnsServers::ordering(&DHCPV6_SERVER1, &DHCPV6_SERVER1), Ordering::Equal);
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
            [NDP_SERVER, DHCP_SERVER, DHCPV6_SERVER1, STATIC_SERVER, UNSPECIFIED_SOURCE_SERVER];
        // We don't compare the last two servers in the list because their ordering is equal
        // w.r.t. eachother.
        for (i, a) in servers[..servers.len() - 2].iter().enumerate() {
            for b in servers[i + 1..].iter() {
                assert_eq!(DnsServers::ordering(a, b), Ordering::Less);
            }
        }

        let mut servers = vec![DHCPV6_SERVER1, DHCP_SERVER, STATIC_SERVER, NDP_SERVER];
        servers.sort_by(DnsServers::ordering);
        assert_eq!(servers, vec![NDP_SERVER, DHCP_SERVER, DHCPV6_SERVER1, STATIC_SERVER]);
    }
}
