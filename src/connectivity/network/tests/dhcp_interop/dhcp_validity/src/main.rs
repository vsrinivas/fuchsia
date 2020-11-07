// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    dhcp_validity_lib::{configure_dhcp_server, verify_v4_addr_present, verify_v6_dns_servers},
    dns_server_watcher::DEFAULT_DNS_PORT,
    fidl_fuchsia_net as fnet, fidl_fuchsia_net_name as fnetname, fuchsia_async as fasync,
    matches::assert_matches,
    net_declare::{fidl_ip, fidl_ip_v6},
    std::time::Duration,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let () = configure_dhcp_server("debian_guest", "/bin/sh -c /root/input/dhcp_setup.sh")
        .await
        .context("configuring DHCP server on Debian guest")?;

    // Configured in dhcpd.conf.
    let want_v4_address = fidl_ip!(192.168.1.10);

    // Configured in dhcpd6.conf.
    let want_v6_dns_servers = vec![
        fnetname::DnsServer_ {
            address: Some(fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
                address: fidl_ip_v6!(1234::5:6),
                zone_index: 0,
                port: DEFAULT_DNS_PORT,
            })),
            source: Some(fnetname::DnsServerSource::Dhcpv6(fnetname::Dhcpv6DnsServerSource {
                source_interface: Some(2),
                ..fnetname::Dhcpv6DnsServerSource::empty()
            })),
            ..fnetname::DnsServer_::empty()
        },
        fnetname::DnsServer_ {
            address: Some(fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
                address: fidl_ip_v6!(7890::12:34),
                zone_index: 0,
                port: DEFAULT_DNS_PORT,
            })),
            source: Some(fnetname::DnsServerSource::Dhcpv6(fnetname::Dhcpv6DnsServerSource {
                source_interface: Some(2),
                ..fnetname::Dhcpv6DnsServerSource::empty()
            })),
            ..fnetname::DnsServer_::empty()
        },
        fnetname::DnsServer_ {
            address: Some(fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
                address: fidl_ip_v6!(fe80::1:2:3:4),
                zone_index: 2,
                port: DEFAULT_DNS_PORT,
            })),
            source: Some(fnetname::DnsServerSource::Dhcpv6(fnetname::Dhcpv6DnsServerSource {
                source_interface: Some(2),
                ..fnetname::Dhcpv6DnsServerSource::empty()
            })),
            ..fnetname::DnsServer_::empty()
        },
    ];

    assert_matches!(
        futures::join!(
            verify_v4_addr_present(want_v4_address, Duration::from_secs(30)),
            verify_v6_dns_servers(2 /* interface_id */, want_v6_dns_servers),
        ),
        (Ok(()), Ok(()))
    );
    Ok(())
}
