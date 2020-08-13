// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_net::{
        IpAddress::{Ipv4, Ipv6},
        Ipv4Address, Ipv6Address, Subnet,
    },
    fidl_fuchsia_net_stack::StackProxy,
    net_types::ip::{Ipv4Addr, Ipv6Addr},
};

pub async fn netstack_did_get_dhcp(
    network_svc: &StackProxy,
    mac_addr: &[u8; 6],
) -> Result<bool, Error> {
    Ok(network_svc
        .list_interfaces()
        .await?
        .into_iter()
        .map(|ni| ni.properties)
        .filter(|p| p.mac.as_ref().map_or(false, |mac| mac.octets == *mac_addr))
        .map(|p| p.addresses)
        .flatten()
        .any(dhcp_ip_filter))
}

fn valid_ip_filter<A: net_types::ip::IpAddress>(addr: &A) -> bool {
    use net_types::{MulticastAddress, SpecifiedAddress};
    !(addr.is_linklocal() || !addr.is_specified() || addr.is_multicast() || addr.is_loopback())
}

pub fn dhcp_ip_filter(ip_addr: Subnet) -> bool {
    fuchsia_syslog::fx_log_info!("checking validity of ip address: {:?}", ip_addr);
    match ip_addr.addr {
        Ipv4(Ipv4Address { addr }) => valid_ip_filter(&Ipv4Addr::new(addr)),
        Ipv6(Ipv6Address { addr }) => valid_ip_filter(&Ipv6Addr::new(addr)),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_net_stack::StackMarker,
        fuchsia_async::Executor,
        futures::{stream::StreamExt, task::Poll},
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    // helper values for tests
    const TEST_IPV4_ADDR_VALID: [u8; 4] = [1; 4];
    const TEST_IPV4_ALL_ZEROS: [u8; 4] = [0; 4];
    const TEST_IPV6_ADDR_VALID: [u8; 16] = [0x1; 16];
    const TEST_IPV6_ALL_ZEROS: [u8; 16] = [0; 16];
    const TEST_IPV6_LINK_LOCAL: [u8; 16] = [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    const TEST_IPV6_MULTICAST: [u8; 16] = [0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

    /// Test to verify a valid ipv4 addr is assigned to an interface.  In the current
    /// implementation, only empty vectors or all zeros are considered to be invalid or unset.
    #[test]
    fn test_single_ipv4_addr_ok() {
        let ipv4_addr =
            Subnet { addr: Ipv4(Ipv4Address { addr: TEST_IPV4_ADDR_VALID }), prefix_len: 0 };
        assert!(dhcp_ip_filter(ipv4_addr));
    }

    /// Test to verify a valid ipv6 addr is assigned to an interface.  In the current
    /// implementation, only empty vectors or all zeros are considered to be invalid or unset.
    #[test]
    fn test_single_ipv6_addr_ok() {
        let ipv6_addr =
            Subnet { addr: Ipv6(Ipv6Address { addr: TEST_IPV6_ADDR_VALID }), prefix_len: 0 };
        assert!(dhcp_ip_filter(ipv6_addr));
    }

    /// IPv4 addresses that are all zeros are considered invalid and should return false when
    /// chacked.
    #[test]
    fn test_single_ipv4_addr_all_zeros_invalid() {
        let ipv4_addr =
            Subnet { addr: Ipv4(Ipv4Address { addr: TEST_IPV4_ALL_ZEROS }), prefix_len: 0 };
        assert_eq!(dhcp_ip_filter(ipv4_addr), false);
    }

    /// IPv6 addresses that are all zeros are considered invalid and should return false when
    /// checked.
    #[test]
    fn test_single_ipv6_addr_all_zeros_invalid() {
        let ipv6_addr =
            Subnet { addr: Ipv6(Ipv6Address { addr: TEST_IPV6_ALL_ZEROS }), prefix_len: 0 };
        assert_eq!(dhcp_ip_filter(ipv6_addr), false);
    }

    #[test]
    fn test_single_ipv6_addr_link_local_invalid() {
        let ipv6_addr =
            Subnet { addr: Ipv6(Ipv6Address { addr: TEST_IPV6_LINK_LOCAL }), prefix_len: 0 };
        assert_eq!(dhcp_ip_filter(ipv6_addr), false);
    }

    #[test]
    fn test_single_ipv6_addr_multicast_invalid() {
        let ipv6_addr =
            Subnet { addr: Ipv6(Ipv6Address { addr: TEST_IPV6_MULTICAST }), prefix_len: 0 };
        assert_eq!(dhcp_ip_filter(ipv6_addr), false);
    }

    fn send_fake_list_iface_response(
        exec: &mut Executor,
        req_stream: &mut fidl_fuchsia_net_stack::StackRequestStream,
        mut iface_info_list: Vec<fidl_fuchsia_net_stack::InterfaceInfo>,
    ) {
        use fidl_fuchsia_net_stack::StackRequest;
        let req = exec.run_until_stalled(&mut req_stream.next());
        let responder = assert_variant!(
            req, Poll::Ready(Some(Ok(StackRequest::ListInterfaces{responder}))) => responder);
        responder.send(&mut iface_info_list.iter_mut()).expect("sending response");
    }

    fn fake_interface_info(
        octets: [u8; 6],
        ip_list: Vec<fidl_fuchsia_net::IpAddress>,
    ) -> fidl_fuchsia_net_stack::InterfaceInfo {
        use fidl_fuchsia_net_stack::*;
        let addresses = ip_list.into_iter().map(|addr| Subnet { addr, prefix_len: 0 }).collect();
        InterfaceInfo {
            id: 0,
            properties: InterfaceProperties {
                name: String::new(),
                topopath: String::new(),
                filepath: String::new(),
                mac: Some(Box::new(fidl_fuchsia_hardware_ethernet::MacAddress { octets })),
                mtu: 0,
                features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                administrative_status: AdministrativeStatus::Enabled,
                physical_status: PhysicalStatus::Up,
                addresses,
            },
        }
    }

    fn run_netstack_did_get_dhcp_test(
        infos: &[([u8; 6], Vec<fidl_fuchsia_net::IpAddress>)],
        mac_to_query: &[u8; 6],
        is_dhcp: bool,
    ) {
        let mut exec = fuchsia_async::Executor::new().expect("creating executor");
        let (proxy, server) =
            fidl::endpoints::create_proxy::<StackMarker>().expect("creating proxy");
        let mut req_stream = server.into_stream().expect("creating stream");
        let iface_addr_fut = netstack_did_get_dhcp(&proxy, mac_to_query);
        pin_mut!(iface_addr_fut);

        assert_variant!(exec.run_until_stalled(&mut iface_addr_fut), Poll::Pending);

        let infos: Vec<_> = infos
            .into_iter()
            .map(|(mac, ips)| fake_interface_info(*mac, (*ips).to_vec()))
            .collect();

        send_fake_list_iface_response(&mut exec, &mut req_stream, infos);

        let got_dhcp = assert_variant!(exec.run_until_stalled(&mut iface_addr_fut),
                                       Poll::Ready(Ok(addrs)) => addrs);
        assert_eq!(got_dhcp, is_dhcp);
    }

    #[test]
    fn test_netstack_did_get_dhcp_no_iface_present() {
        run_netstack_did_get_dhcp_test(&[], &[1, 2, 3, 4, 5, 6], false);
    }

    #[test]
    fn test_netstack_did_get_dhcp_one_iface_match() {
        let ip = Ipv4(Ipv4Address { addr: TEST_IPV4_ADDR_VALID });
        let infos = [([1, 2, 3, 4, 5, 6], vec![ip])];

        run_netstack_did_get_dhcp_test(&infos[..], &[1, 2, 3, 4, 5, 6], true);
    }

    #[test]
    fn test_netstack_did_get_dhcp_one_iface_mismatch() {
        let infos = [([0; 6], vec![Ipv4(Ipv4Address { addr: TEST_IPV4_ADDR_VALID })])];

        run_netstack_did_get_dhcp_test(&infos[..], &[1, 2, 3, 4, 5, 6], false);
    }

    #[test]
    fn test_netstack_did_get_dhcp_one_iface_match_one_mismatch() {
        let ip_match = Ipv4(Ipv4Address { addr: TEST_IPV4_ADDR_VALID });
        let ip_mismatch = Ipv4(Ipv4Address { addr: TEST_IPV4_ALL_ZEROS });
        let infos = [([1, 2, 3, 4, 5, 6], vec![ip_match]), ([0; 6], vec![ip_mismatch])];

        run_netstack_did_get_dhcp_test(&infos[..], &[1, 2, 3, 4, 5, 6], true);
    }

    #[test]
    fn test_netstack_did_get_dhcp_two_ifaces_match() {
        let ip1 = Ipv4(Ipv4Address { addr: TEST_IPV4_ADDR_VALID });
        let ip2 = Ipv4(Ipv4Address { addr: TEST_IPV4_ALL_ZEROS });
        let infos = [([1, 2, 3, 4, 5, 6], vec![ip1]), ([1, 2, 3, 4, 5, 6], vec![ip2])];

        run_netstack_did_get_dhcp_test(&infos[..], &[1, 2, 3, 4, 5, 6], true);
    }

    #[test]
    fn test_netstack_did_get_dhcp_all_ips_invalid() {
        let ip_list = vec![
            Ipv4(Ipv4Address { addr: TEST_IPV4_ALL_ZEROS }),
            Ipv6(Ipv6Address { addr: TEST_IPV6_ALL_ZEROS }),
            Ipv6(Ipv6Address { addr: TEST_IPV6_LINK_LOCAL }),
            Ipv6(Ipv6Address { addr: TEST_IPV6_MULTICAST }),
        ];

        let infos = [([1, 2, 3, 4, 5, 6], ip_list)];

        run_netstack_did_get_dhcp_test(&infos[..], &[1, 2, 3, 4, 5, 6], false);
    }
}
