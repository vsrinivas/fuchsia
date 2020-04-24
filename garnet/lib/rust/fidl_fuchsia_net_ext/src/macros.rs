// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// `ipv4_addr![b0, b1, b2, b3]` declares a [`fidl_fuchsia_net::Ipv4Address`]
/// with IP address b0.b1.b2.b3
#[macro_export]
macro_rules! ipv4_addr {
    [$b0:expr, $b1:expr, $b2:expr, $b3:expr] => {
        fidl_fuchsia_net::Ipv4Address { addr: [$b0, $b1, $b2, $b3] }
    }
}

/// `ipv6_addr![b0, b1, ..., b14, b15]`, where `bn` is a `u8``, declares a
/// [`fidl_fuchsia_net::Ipv6Address`] with IP address b0b1::...::b14b15.
#[macro_export]
macro_rules! ipv6_addr {
    [$b0:expr, $b1:expr, $b2:expr, $b3:expr, $b4:expr, $b5:expr, $b6:expr, $b7:expr, $b8:expr, $b9:expr, $b10:expr, $b11:expr, $b12:expr, $b13:expr, $b14:expr, $b15:expr] => {
        fidl_fuchsia_net::Ipv6Address { addr: [$b0, $b1, $b2, $b3, $b4, $b5,
        $b6, $b7, $b8, $b9, $b10, $b11, $b12, $b13, $b14, $b15] }
    };
}

/// Declares either a [`fidl_fuchsia_net::IpAddress::Ipv4`] or
/// [`fidl_fuchsia_net::IpAddress::Ipv6`] depending on the number of bytes
/// given.
#[macro_export]
macro_rules! ip_addr {
    [$b0:expr, $b1:expr, $b2:expr, $b3:expr] => {
        fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net_ext::ipv4_addr![$b0, $b1, $b2, $b3])
    };
    [$b0:expr, $b1:expr, $b2:expr, $b3:expr, $b4:expr, $b5:expr, $b6:expr, $b7:expr, $b8:expr, $b9:expr, $b10:expr, $b11:expr, $b12:expr, $b13:expr, $b14:expr, $b15:expr] => {
        fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net_ext::ipv6_addr![$b0, $b1, $b2, $b3, $b4, $b5,
        $b6, $b7, $b8, $b9, $b10, $b11, $b12, $b13, $b14, $b15])
    };
}

#[cfg(test)]
mod tests {
    use crate as fidl_fuchsia_net_ext;
    use fidl_fuchsia_net;

    #[test]
    fn test_address_declarations() {
        let v4: fidl_fuchsia_net::Ipv4Address = ipv4_addr![192, 168, 0, 1];
        assert_eq!(v4.addr, [192, 168, 0, 1]);

        let v6: fidl_fuchsia_net::Ipv6Address =
            ipv6_addr![0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
        assert_eq!(v6.addr, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);

        let addr_v4: fidl_fuchsia_net::IpAddress = ip_addr![192, 168, 0, 1];
        assert_eq!(addr_v4, fidl_fuchsia_net::IpAddress::Ipv4(v4));

        let addr_v6: fidl_fuchsia_net::IpAddress =
            ip_addr![0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
        assert_eq!(addr_v6, fidl_fuchsia_net::IpAddress::Ipv6(v6));
    }
}
