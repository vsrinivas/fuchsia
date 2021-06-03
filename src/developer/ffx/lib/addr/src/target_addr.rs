// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::*,
    fidl_fuchsia_developer_bridge::{TargetAddrInfo, TargetIp},
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address, Subnet},
    netext::{scope_id_to_name, IsLocalAddr},
    std::cmp::Ordering,
    std::net::{IpAddr, SocketAddr, SocketAddrV4, SocketAddrV6},
};

#[derive(Hash, Clone, Debug, Copy, Eq, PartialEq)]
pub struct TargetAddr {
    ip: IpAddr,
    scope_id: u32,
}

impl Ord for TargetAddr {
    fn cmp(&self, other: &Self) -> Ordering {
        let this_socket = SocketAddr::from(self);
        let other_socket = SocketAddr::from(other);
        this_socket.cmp(&other_socket)
    }
}

impl PartialOrd for TargetAddr {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Into<TargetAddrInfo> for &TargetAddr {
    fn into(self) -> TargetAddrInfo {
        TargetAddrInfo::Ip(TargetIp {
            ip: match self.ip {
                IpAddr::V6(i) => IpAddress::Ipv6(Ipv6Address { addr: i.octets().into() }),
                IpAddr::V4(i) => IpAddress::Ipv4(Ipv4Address { addr: i.octets().into() }),
            },
            scope_id: self.scope_id,
        })
    }
}

impl Into<TargetAddrInfo> for TargetAddr {
    fn into(self) -> TargetAddrInfo {
        (&self).into()
    }
}

impl From<TargetAddrInfo> for TargetAddr {
    fn from(t: TargetAddrInfo) -> Self {
        (&t).into()
    }
}

impl From<&TargetAddrInfo> for TargetAddr {
    fn from(t: &TargetAddrInfo) -> Self {
        let (addr, scope): (IpAddr, u32) = match t {
            TargetAddrInfo::Ip(ip) => match ip.ip {
                IpAddress::Ipv6(Ipv6Address { addr }) => (addr.into(), ip.scope_id),
                IpAddress::Ipv4(Ipv4Address { addr }) => (addr.into(), ip.scope_id),
            },
            TargetAddrInfo::IpPort(ip) => match ip.ip {
                IpAddress::Ipv6(Ipv6Address { addr }) => (addr.into(), ip.scope_id),
                IpAddress::Ipv4(Ipv4Address { addr }) => (addr.into(), ip.scope_id),
            },
            // TODO(fxbug.dev/52733): Add serial numbers.,
        };

        (addr, scope).into()
    }
}

impl From<Subnet> for TargetAddr {
    fn from(i: Subnet) -> Self {
        // TODO(awdavies): Figure out if it's possible to get the scope_id from
        // this address.
        match i.addr {
            IpAddress::Ipv4(ip4) => SocketAddr::from((ip4.addr, 0)).into(),
            IpAddress::Ipv6(ip6) => SocketAddr::from((ip6.addr, 0)).into(),
        }
    }
}

impl From<TargetAddr> for SocketAddr {
    fn from(t: TargetAddr) -> Self {
        Self::from(&t)
    }
}

impl From<&TargetAddr> for SocketAddr {
    fn from(t: &TargetAddr) -> Self {
        match t.ip {
            IpAddr::V6(addr) => SocketAddr::V6(SocketAddrV6::new(addr, 0, 0, t.scope_id)),
            IpAddr::V4(addr) => SocketAddr::V4(SocketAddrV4::new(addr, 0)),
        }
    }
}

impl From<(IpAddr, u32)> for TargetAddr {
    fn from(f: (IpAddr, u32)) -> Self {
        Self { ip: f.0, scope_id: f.1 }
    }
}

impl From<SocketAddr> for TargetAddr {
    fn from(s: SocketAddr) -> Self {
        Self {
            ip: s.ip(),
            scope_id: match s {
                SocketAddr::V6(addr) => addr.scope_id(),
                _ => 0,
            },
        }
    }
}

impl TargetAddr {
    /// Construct a new TargetAddr from a string representation of the form
    /// accepted by std::net::SocketAddr, e.g. 127.0.0.1:22, or [fe80::1%1]:0.
    pub fn new<S>(s: S) -> Result<Self>
    where
        S: AsRef<str>,
    {
        let sa = s.as_ref().parse::<SocketAddr>()?;
        Ok(Self::from(sa))
    }

    pub fn scope_id(&self) -> u32 {
        self.scope_id
    }

    pub fn set_scope_id(&mut self, scope_id: u32) {
        self.scope_id = scope_id
    }

    pub fn ip(&self) -> IpAddr {
        self.ip.clone()
    }
}

impl std::fmt::Display for TargetAddr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.ip())?;

        if self.ip.is_link_local_addr() && self.scope_id() > 0 {
            write!(f, "%{}", scope_id_to_name(self.scope_id()))?;
        }

        Ok(())
    }
}
