// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use net;
use netstack;
use std::convert::From;
use std::fmt;

struct IpAddress<'a>(&'a net::IpAddress);

impl<'a> fmt::Display for IpAddress<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        match self.0 {
            net::IpAddress::Ipv4(a) => {
                write!(f, "{}.{}.{}.{}", a.addr[0], a.addr[1], a.addr[2], a.addr[3])
            }
            // TODO(tkilbourn): better pretty-printing for IPv6 addresses
            #[cfg_attr(rustfmt, rustfmt_skip)]
            net::IpAddress::Ipv6(a) => write!(
                f,
                "{:x}{:x}:{:x}{:x}:{:x}{:x}:{:x}{:x}:{:x}{:x}:{:x}{:x}:{:x}{:x}:{:x}{:x}",
                a.addr[0], a.addr[1], a.addr[2], a.addr[3],
                a.addr[4], a.addr[5], a.addr[6], a.addr[7],
                a.addr[8], a.addr[9], a.addr[10], a.addr[11],
                a.addr[12], a.addr[13], a.addr[14], a.addr[15],
            ),
        }
    }
}

pub struct InterfaceAddress<'a>(&'a netstack::InterfaceAddress);

impl<'a> From<&'a netstack::InterfaceAddress> for InterfaceAddress<'a> {
    fn from(a: &'a netstack::InterfaceAddress) -> Self {
        InterfaceAddress(a)
    }
}

impl<'a> fmt::Display for InterfaceAddress<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        write!(f, "{}/{}", IpAddress(&self.0.ip_address), self.0.prefix_len)
    }
}

pub struct InterfaceInfo(netstack::InterfaceInfo);

impl From<netstack::InterfaceInfo> for InterfaceInfo {
    fn from(i: netstack::InterfaceInfo) -> Self {
        InterfaceInfo(i)
    }
}

impl fmt::Display for InterfaceInfo {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        write!(f, "Interface Info\n")?;
        write!(f, "  id: {}\n", self.0.id)?;
        write!(f, "  path: {}\n", self.0.path)?;
        if let Some(mac) = &self.0.mac {
            write!(
                f,
                "  mac: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}\n",
                mac.addr[0], mac.addr[1], mac.addr[2], mac.addr[3], mac.addr[4], mac.addr[5]
            )?;
        }
        write!(f, "  mtu: {}\n", self.0.mtu)?;
        write!(f, "  features: ")?;
        for (i, feature) in self.0.features.iter().enumerate() {
            if i > 0 {
                write!(f, " ,")?;
            }
            write!(f, "{:?}", feature)?;
        }
        write!(f, "\n")?;
        if self.0.status & netstack::INTERFACE_STATUS_ENABLED != 0 {
            write!(f, "  Enabled\n")?;
        } else {
            write!(f, "  Disabled\n")?;
        }
        if self.0.status & netstack::INTERFACE_STATUS_LINK_UP != 0 {
            write!(f, "  Link up\n")?;
        } else {
            write!(f, "  Link down\n")?;
        }
        write!(f, "  Addresses:")?;
        for addr in &self.0.addresses {
            write!(f, "\n    {}", InterfaceAddress(addr))?;
        }
        Ok(())
    }
}
