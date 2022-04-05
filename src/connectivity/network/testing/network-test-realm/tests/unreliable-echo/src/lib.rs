// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The SocketAddr that unreliable-echo listens on.
/// This is only a function because SocketAddrV4 cannot yet be constructed const
/// on stable.
pub fn socket_addr_v4() -> std::net::SocketAddr {
    (std::net::Ipv4Addr::LOCALHOST, 10000).into()
}

/// The SocketAddr that unreliable-echo-v6 listens on.
/// This is only a function because SocketAddrV6 cannot yet be constructed const
/// on stable.
pub fn socket_addr_v6() -> std::net::SocketAddr {
    (std::net::Ipv6Addr::LOCALHOST, 10000).into()
}
