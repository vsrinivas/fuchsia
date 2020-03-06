// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::net::IpAddr;
use std::net::Ipv6Addr;

use libc;

pub trait IsLinkLocal {
    fn is_ll(&self) -> bool;
}

impl IsLinkLocal for Ipv6Addr {
    /// Returns true if this is a link local address:
    /// https://tools.ietf.org/html/rfc4291
    fn is_ll(&self) -> bool {
        self.segments()[0] == 0xfe80
            && self.segments()[1] == 0x0000
            && self.segments()[2] == 0x0000
            && self.segments()[3] == 0x0000
    }
}

pub trait IsMcast {
    /// Determines if some kind of flags or some interface is multicast.
    fn is_mcast(&self) -> bool;
}

impl IsMcast for i32 {
    /// Tests if the ifa_flags portion of an ifaddrs struct is deemed usable
    /// for multicast:
    /// -- The interface is up.
    /// -- Mcast bit is set.
    /// -- The interface is not a loopback.
    fn is_mcast(&self) -> bool {
        self & libc::IFF_UP != 0
            && self & libc::IFF_LOOPBACK == 0
            && self & libc::IFF_MULTICAST != 0
    }
}

/// An Mcast interface is:
/// -- Not a loopback
/// -- Up (as opposed to down).
/// -- Has mcast enabled
#[derive(Debug, Hash, Clone, Eq, PartialEq)]
pub struct McastInterface {
    pub name: String,
    pub id: u32,
    pub addrs: Vec<IpAddr>,
}

// TODO(fxb/44855): This needs to be e2e tested.
#[cfg(target_os = "linux")]
pub mod linux {
    use super::*;
    use std::collections::HashMap;
    use std::io;
    use std::mem;

    pub fn sockaddr_to_ip(sockaddr: *const libc::sockaddr) -> Option<IpAddr> {
        if sockaddr.is_null() {
            return None;
        }
        let family = i32::from(unsafe { *sockaddr }.sa_family);
        match family {
            libc::AF_INET => {
                let sockaddr = &unsafe { *(sockaddr as *const libc::sockaddr_in) };
                let addr_raw = sockaddr.sin_addr.s_addr;
                // `to_be()` should be a no-op here, but just in case.
                Some(IpAddr::V4(addr_raw.to_be().into()))
            }
            libc::AF_INET6 => {
                let sockaddr = &unsafe { *(sockaddr as *const libc::sockaddr_in6) };
                let ip: Ipv6Addr = sockaddr.sin6_addr.s6_addr.into();
                // Only link-local is supported for the kind of mcast we're going
                // to be doing. Globally routable addresses appear to panic when
                // calling bind() in UdpBuilder.
                if ip.is_ll() {
                    Some(IpAddr::V6(ip))
                } else {
                    None
                }
            }
            _ => None,
        }
    }

    pub unsafe fn get_mcast_interfaces() -> io::Result<Vec<McastInterface>> {
        let mut res = HashMap::<u32, McastInterface>::new();
        let mut ifaddrs = mem::MaybeUninit::uninit().as_mut_ptr();
        if -1 == libc::getifaddrs(&mut ifaddrs) {
            return Err(io::Error::last_os_error());
        }

        loop {
            if ifaddrs.is_null() {
                break;
            }
            if let Some(ip) = sockaddr_to_ip((*ifaddrs).ifa_addr) {
                let name = std::ffi::CStr::from_ptr((*ifaddrs).ifa_name as *const _)
                    .to_string_lossy()
                    .into_owned();
                let id = libc::if_nametoindex((*ifaddrs).ifa_name as *const _);
                let flags = (*ifaddrs).ifa_flags as i32;

                if flags.is_mcast() {
                    match res.get_mut(&id) {
                        Some(iface) => iface.addrs.push(ip),
                        None => {
                            res.insert(id, McastInterface { name, id, addrs: vec![ip] });
                        }
                    }
                }
            }
            ifaddrs = (*ifaddrs).ifa_next;
        }
        libc::freeifaddrs(ifaddrs);
        Ok(res.iter().map(|(_, v)| v.clone()).collect())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_is_link_local() {
        let is_ll = Ipv6Addr::new(0xfe80, 0, 0, 0, 1, 6, 7, 8);
        let is_not_ll = Ipv6Addr::new(0xfe81, 2, 3, 4, 5, 6, 7, 8);
        assert!(is_ll.is_ll());
        assert!(!is_not_ll.is_ll());
    }

    #[test]
    fn test_mcast_flags() {
        // Expected interface.
        assert!((libc::IFF_UP | libc::IFF_MULTICAST).is_mcast());
        // Loopback.
        assert!(!(libc::IFF_UP | libc::IFF_MULTICAST | libc::IFF_LOOPBACK).is_mcast());
        // Down interface.
        assert!(!(libc::IFF_MULTICAST).is_mcast());
    }
}
