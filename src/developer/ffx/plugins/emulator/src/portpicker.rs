// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::net::{Ipv4Addr, Ipv6Addr, SocketAddrV4, SocketAddrV6, TcpListener, ToSocketAddrs};

pub type Port = u16;

/// Asks the OS for a free port
fn ask_free_tcp_port() -> Option<Port> {
    let ipv4 = SocketAddrV4::new(Ipv4Addr::LOCALHOST, 0);
    let ipv6 = SocketAddrV6::new(Ipv6Addr::LOCALHOST, 0, 0, 0);
    bind_tcp(ipv6).or_else(|| bind_tcp(ipv4))
}

// Try to bind to a socket using TCP
fn bind_tcp<A: ToSocketAddrs>(addr: A) -> Option<Port> {
    match TcpListener::bind(addr).map(|s| s.local_addr().map(|a| a.port())) {
        Ok(port) => port.ok(),
        Err(_) => None,
    }
}

/// Picks an available tcp port
pub fn pick_unused_port() -> Option<Port> {
    for _ in 0..10 {
        if let Some(port) = ask_free_tcp_port() {
            return Some(port);
        }
    }
    None
}
