// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as fidl;

#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub struct IpAddress(pub std::net::IpAddr);

impl std::fmt::Display for IpAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        let IpAddress(ip_address) = self;
        write!(f, "{}", ip_address)
    }
}

impl From<fidl::IpAddress> for IpAddress {
    fn from(addr: fidl::IpAddress) -> IpAddress {
        IpAddress(match addr {
            fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr }) => addr.into(),
            fidl::IpAddress::Ipv6(fidl::Ipv6Address { addr }) => addr.into(),
        })
    }
}

impl Into<fidl::IpAddress> for IpAddress {
    fn into(self) -> fidl::IpAddress {
        let IpAddress(ip_address) = self;
        match ip_address {
            std::net::IpAddr::V4(v4addr) => {
                fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr: v4addr.octets() })
            }
            std::net::IpAddr::V6(v6addr) => {
                fidl::IpAddress::Ipv6(fidl::Ipv6Address { addr: v6addr.octets() })
            }
        }
    }
}

impl std::str::FromStr for IpAddress {
    type Err = failure::Error;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(IpAddress(s.parse()?))
    }
}

#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub struct Subnet {
    addr: IpAddress,
    prefix_len: u8,
}

impl std::fmt::Display for Subnet {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        let Self { addr, prefix_len } = self;
        write!(f, "{}/{}", addr, prefix_len)
    }
}

impl std::str::FromStr for Subnet {
    type Err = failure::Error;

    // Parse a Subnet from a CIDR-notated IP address.
    //
    // NB: if we need additional CIDR related functionality in the future,
    // we should consider pulling in https://crates.io/crates/cidr
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut pieces = s.split('/');
        let addr = pieces
            .next()
            .expect("String#split should never return an empty iterator")
            .parse::<std::net::IpAddr>()?;

        let addr_len = match addr {
            std::net::IpAddr::V4(_) => 32,
            std::net::IpAddr::V6(_) => 128,
        };
        let validated_prefix = match pieces.next() {
            Some(p) => {
                let parsed_len = p.parse::<u8>()?;
                if parsed_len > addr_len {
                    Err(failure::format_err!(
                        "prefix length provided ({} bits) too large. address {} is only {} bits long",
                        parsed_len,
                        addr,
                        addr_len
                    ))
                } else {
                    Ok(parsed_len)
                }
            }
            None => Ok(addr_len),
        };

        let () = match pieces.next() {
            Some(_) => Err(failure::format_err!(
                "more than one '/' separator found while attempting to parse CIDR string {}",
                s
            )),
            None => Ok(()),
        }?;
        let addr = IpAddress(addr);
        Ok(Subnet { addr, prefix_len: validated_prefix? })
    }
}

impl From<fidl::Subnet> for Subnet {
    fn from(subnet: fidl::Subnet) -> Self {
        let fidl::Subnet { addr, prefix_len } = subnet;
        let addr = addr.into();
        Self { addr, prefix_len }
    }
}

impl Into<fidl::Subnet> for Subnet {
    fn into(self) -> fidl::Subnet {
        let Self { addr, prefix_len } = self;
        let addr = addr.into();
        fidl::Subnet { addr, prefix_len }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::str::FromStr;

    #[test]
    fn test_ipaddr() {
        let want_ext = IpAddress(std::net::IpAddr::V4(std::net::Ipv4Addr::new(1, 2, 3, 4)));
        let want_fidl = fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr: [1, 2, 3, 4] });
        let got_fidl: fidl::IpAddress = want_ext.into();
        let got_ext = IpAddress::from(got_fidl);

        assert_eq!(want_ext, got_ext);
        assert_eq!(want_fidl, got_fidl);
    }

    #[test]
    fn test_subnet() {
        let err_str_subnets = vec![
            // Note "1.2.3.4" or "::" is a valid form. Subnet's FromStr trait allows
            // missing prefix, and assumes the legally maximum prefix length.
            "",
            "/32",                              // no ip address
            " /32",                             // no ip address
            "1.2.3.4/8/8",                      // too many slashes
            "1.2.3.4/33",                       // prefix too long
            "192.168.32.1:8080",                // that's a port, not a prefix
            "e80::e1bf:4fe9:fb62:e3f4/129",     // prefix too long
            "e80::e1bf:4fe9:fb62:e3f4/32%eth0", // zone index
        ];
        for e in err_str_subnets {
            if Subnet::from_str(e).is_ok() {
                eprintln!(
                    "a malformed str is wrongfully convertitable to Subnet struct: \"{}\"",
                    e
                );
                assert!(false);
            }
        }

        let want_str = "1.2.3.4/18";
        let want_ext = Subnet {
            addr: IpAddress(std::net::IpAddr::V4(std::net::Ipv4Addr::new(1, 2, 3, 4))),
            prefix_len: 18,
        };
        let want_fidl = fidl::Subnet {
            addr: fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr: [1, 2, 3, 4] }),
            prefix_len: 18,
        };

        let got_ext = Subnet::from_str(want_str).ok().expect("conversion error");
        let got_fidl: fidl::Subnet = got_ext.into();
        let got_ext_back = Subnet::from(got_fidl);
        let got_str = &format!("{}", got_ext_back);

        assert_eq!(want_ext, got_ext);
        assert_eq!(want_fidl, got_fidl);
        assert_eq!(got_ext, got_ext_back);
        assert_eq!(want_str, got_str);
    }
}
