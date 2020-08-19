// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::{Context as _, Result};
use itertools::Itertools;
use nix::{
    ifaddrs::{getifaddrs, InterfaceAddress},
    net::if_::InterfaceFlags,
    sys::socket::SockAddr,
};
use std::net::SocketAddr;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

pub trait IsLocalAddr {
    /// is_local_addr returns true if the address is not globally routable.
    fn is_local_addr(&self) -> bool;

    /// is_link_local_addr returns true if the address is an IPv6 link local address.
    fn is_link_local_addr(&self) -> bool;
}

impl IsLocalAddr for IpAddr {
    fn is_local_addr(&self) -> bool {
        match self {
            IpAddr::V4(ref ip) => ip.is_local_addr(),
            IpAddr::V6(ref ip) => ip.is_local_addr(),
        }
    }

    fn is_link_local_addr(&self) -> bool {
        match self {
            IpAddr::V4(ref ip) => ip.is_link_local_addr(),
            IpAddr::V6(ref ip) => ip.is_link_local_addr(),
        }
    }
}

impl IsLocalAddr for Ipv4Addr {
    fn is_local_addr(&self) -> bool {
        // TODO(58517): add the various RFC reserved addresses and ranges too
        match self.octets() {
            [10, _, _, _] => true,
            [172, 16..=31, _, _] => true,
            [192, 168, _, _] => true,
            [169, 254, 1..=254, _] => true,
            _ => false,
        }
    }

    fn is_link_local_addr(&self) -> bool {
        false
    }
}

impl IsLocalAddr for Ipv6Addr {
    fn is_local_addr(&self) -> bool {
        let segments = self.segments();

        // ULA
        if segments[0] & 0xfe00 == 0xfc00 {
            return true;
        }

        self.is_link_local_addr()
    }

    fn is_link_local_addr(&self) -> bool {
        let segments = self.segments();

        return segments[0] & 0xffff == 0xfe80
            && segments[1] & 0xffff == 0
            && segments[2] & 0xffff == 0
            && segments[3] & 0xffff == 0;
    }
}

/// An Mcast interface is:
/// -- Not a loopback.
/// -- Up (as opposed to down).
/// -- Has mcast enabled.
/// -- Has at least one non-globally routed address.
#[derive(Debug, Hash, Clone, Eq, PartialEq)]
pub struct McastInterface {
    pub name: String,
    pub addrs: Vec<SocketAddr>,
}

fn is_local_multicast_addr(addr: &InterfaceAddress) -> bool {
    let inet_addr = match addr.address {
        Some(SockAddr::Inet(inet)) => inet,
        _ => return false,
    };

    if !(addr.flags.contains(InterfaceFlags::IFF_UP)
        && addr.flags.contains(InterfaceFlags::IFF_MULTICAST)
        && !addr.flags.contains(InterfaceFlags::IFF_LOOPBACK))
    {
        return false;
    }

    inet_addr.ip().to_std().is_local_addr()
}

// ifaddr_to_socketaddr returns Some(std::net::SocketAddr) if ifaddr contains an inet addr, none otherwise.
fn ifaddr_to_socketaddr(ifaddr: InterfaceAddress) -> Option<std::net::SocketAddr> {
    match ifaddr.address {
        Some(SockAddr::Inet(sockaddr)) => Some(sockaddr.to_std()),
        _ => None,
    }
}

// select_mcast_interfaces iterates over a set of IterfaceAddresses,
// selecting only those that meet the McastInterface criteria (see
// McastInterface), and returns them in a McastInterface representation.
fn select_mcast_interfaces(
    iter: &mut dyn Iterator<Item = InterfaceAddress>,
) -> Vec<McastInterface> {
    iter.filter(is_local_multicast_addr)
        .sorted_by_key(|ifaddr| ifaddr.interface_name.to_string())
        .group_by(|ifaddr| ifaddr.interface_name.to_string())
        .into_iter()
        .map(|(name, ifaddrs)| McastInterface {
            name: name.to_string(),
            addrs: ifaddrs.filter_map(ifaddr_to_socketaddr).collect(),
        })
        .collect()
}

/// get_mcast_interfaces retrieves all local interfaces that are local
/// multicast enabled. See McastInterface for more detials.
// TODO(fxb/44855): This needs to be e2e tested.
pub fn get_mcast_interfaces() -> Result<Vec<McastInterface>> {
    Ok(select_mcast_interfaces(&mut getifaddrs().context("Failed to get all interface addresses")?))
}

#[cfg(test)]
mod tests {
    use super::*;
    use nix::sys::socket::InetAddr;
    use std::str::FromStr;

    fn sockaddr(s: &str) -> SockAddr {
        SockAddr::new_inet(InetAddr::from_std(&SocketAddr::from_str(s).unwrap()))
    }

    #[test]
    fn test_select_mcast_interfaces() {
        let multicast_interface = InterfaceAddress {
            interface_name: "test-interface".to_string(),
            flags: InterfaceFlags::IFF_UP | InterfaceFlags::IFF_MULTICAST,
            address: Some(sockaddr("192.168.0.1:1234")),
            netmask: Some(sockaddr("255.255.255.0:0")),
            broadcast: None,
            destination: None,
        };

        let mut down_interface = multicast_interface.clone();
        down_interface.interface_name = "down-interface".to_string();
        down_interface.flags.remove(InterfaceFlags::IFF_UP);

        let mut mult_disabled = multicast_interface.clone();
        mult_disabled.interface_name = "no_multi-interface".to_string();
        mult_disabled.flags.remove(InterfaceFlags::IFF_MULTICAST);

        let mut no_addr = multicast_interface.clone();
        no_addr.interface_name = "no_addr-interface".to_string();
        no_addr.address = None;

        let mut mult2 = multicast_interface.clone();
        mult2.interface_name = "test-interface2".to_string();

        let mut addr2 = multicast_interface.clone();
        addr2.address = Some(sockaddr("192.168.0.2:1234"));

        let interfaces =
            vec![multicast_interface, mult2, addr2, down_interface, mult_disabled, no_addr];

        let result = select_mcast_interfaces(&mut interfaces.into_iter());
        assert_eq!(2, result.len());

        let ti = result.iter().find(|mcast| mcast.name == "test-interface");
        assert!(ti.is_some());
        assert!(result.iter().find(|mcast| mcast.name == "test-interface2").is_some());

        let ti_addrs =
            ti.unwrap().addrs.iter().map(|addr| addr.to_string()).sorted().collect::<Vec<String>>();
        assert_eq!(ti_addrs, ["192.168.0.1:1234", "192.168.0.2:1234"]);
    }

    #[test]
    fn test_is_local_multicast_addr() {
        let multicast_interface = InterfaceAddress {
            interface_name: "test-interface".to_string(),
            flags: InterfaceFlags::IFF_UP | InterfaceFlags::IFF_MULTICAST,
            address: Some(sockaddr("192.168.0.1:1234")),
            netmask: Some(sockaddr("255.255.255.0:0")),
            broadcast: None,
            destination: None,
        };

        assert!(is_local_multicast_addr(&multicast_interface));

        let mut down_interface = multicast_interface.clone();
        down_interface.flags.remove(InterfaceFlags::IFF_UP);
        assert!(!is_local_multicast_addr(&down_interface));

        let mut mult_disabled = multicast_interface.clone();
        mult_disabled.flags.remove(InterfaceFlags::IFF_MULTICAST);
        assert!(!is_local_multicast_addr(&mult_disabled));

        let mut no_addr = multicast_interface.clone();
        no_addr.address = None;
        assert!(!is_local_multicast_addr(&no_addr));
    }

    #[test]
    fn test_is_local_addr() {
        let local_addresses = vec![
            IpAddr::V4(Ipv4Addr::new(192, 168, 0, 1)),
            IpAddr::V6(Ipv6Addr::new(0xfe80, 0, 0, 0, 1, 6, 7, 8)),
        ];
        let not_local_addresses = vec![
            IpAddr::V4(Ipv4Addr::new(8, 8, 8, 8)),
            IpAddr::V6(Ipv6Addr::new(0x2607, 0xf8b0, 0x4005, 0x805, 0, 0, 0, 0x200e)),
        ];

        for addr in local_addresses {
            assert!(&addr.is_local_addr());
        }
        for addr in not_local_addresses {
            assert!(!&addr.is_local_addr());
        }
    }

    #[test]
    fn test_is_link_local_addr() {
        let link_local_addresses = vec![IpAddr::V6(Ipv6Addr::new(0xfe80, 0, 0, 0, 1, 6, 7, 8))];
        let not_link_local_addresses = vec![
            IpAddr::V4(Ipv4Addr::new(192, 168, 0, 1)),
            IpAddr::V4(Ipv4Addr::new(8, 8, 8, 8)),
            IpAddr::V6(Ipv6Addr::new(0x2607, 0xf8b0, 0x4005, 0x805, 0, 0, 0, 0x200e)),
        ];

        for addr in link_local_addresses {
            assert!(&addr.is_link_local_addr());
        }
        for addr in not_link_local_addresses {
            assert!(!&addr.is_link_local_addr());
        }
    }

    #[test]
    fn test_is_local_v4() {
        let local_addresses = vec![
            Ipv4Addr::new(192, 168, 0, 1),
            Ipv4Addr::new(10, 0, 0, 1),
            Ipv4Addr::new(172, 16, 0, 1),
        ];

        let not_local_addresses = vec![
            Ipv4Addr::new(8, 8, 8, 8),
            Ipv4Addr::new(4, 4, 4, 4),
            Ipv4Addr::new(1, 1, 1, 1),
            Ipv4Addr::new(224, 1, 1, 1),
        ];

        for addr in local_addresses {
            assert!(&addr.is_local_addr());
        }
        for addr in not_local_addresses {
            assert!(!&addr.is_local_addr());
        }
    }

    #[test]
    fn test_is_local_v6() {
        let local_addresses = vec![
            Ipv6Addr::new(0xfe80, 0, 0, 0, 1, 6, 7, 8),
            Ipv6Addr::new(0xfc07, 0, 0, 0, 1, 6, 7, 8),
        ];

        let not_local_addresses = vec![
            Ipv6Addr::new(0xfe81, 0, 0, 0, 1, 6, 7, 8),
            Ipv6Addr::new(0xfe79, 0, 0, 0, 1, 6, 7, 8),
            Ipv6Addr::new(0x2607, 0xf8b0, 0x4005, 0x805, 0, 0, 0, 0x200e),
        ];

        for addr in local_addresses {
            assert!(&addr.is_local_addr());
        }
        for addr in not_local_addresses {
            assert!(!&addr.is_local_addr());
        }
    }
}
