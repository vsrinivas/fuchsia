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
            net::IpAddress::Ipv4(a) => write!(
                f,
                "{}",
                ::std::net::IpAddr::V4(::std::net::Ipv4Addr::from(a.addr))
            ),
            net::IpAddress::Ipv6(a) => write!(
                f,
                "{}",
                ::std::net::IpAddr::V6(::std::net::Ipv6Addr::from(a.addr))
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

pub struct ForwardingEntry(netstack::ForwardingEntry);

impl From<netstack::ForwardingEntry> for ForwardingEntry {
    fn from(e: netstack::ForwardingEntry) -> Self {
        ForwardingEntry(e)
    }
}

impl fmt::Display for ForwardingEntry {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        write!(
            f,
            "{}/{}: ",
            IpAddress(&self.0.subnet.addr),
            self.0.subnet.prefix_len
        )?;
        match self.0.destination {
            netstack::ForwardingDestination::DeviceId(id) => write!(f, "device id {}", id),
            netstack::ForwardingDestination::NextHop(ref nh) => {
                write!(f, "next hop {}", IpAddress(&nh))
            }
        }
    }
}
