// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    io::IoSlice,
    net::{Ipv6Addr, SocketAddr},
};

use fuchsia_async as fasync;

use tracing::info;

const BUS_NAME: &str = "test-bus";
const SERVER_NAME: &str = "server";

const SERVER_PORT: u16 = 1234;

const RESPONSE_PREFIX: &str = "Response: ";

// Server that simply pings back any data it gets with a prefix.
#[fuchsia::main]
async fn main() {
    let sockaddr = SocketAddr::new(Ipv6Addr::UNSPECIFIED.into(), SERVER_PORT);
    let socket = fasync::net::UdpSocket::bind(&sockaddr).expect("bind UDP socket");

    // Let the client know that we are listening for incoming connections.
    let _bus = netemul_sync::Bus::subscribe(BUS_NAME, SERVER_NAME).expect("subscribe to bus");
    info!("waiting for datagrams on {}...", socket.local_addr().expect("get socket local addr"));

    loop {
        let mut recv_buf = [0; 1024];
        let (read, remote_addr) = socket.recv_from(&mut recv_buf).await.expect("recv from socket");
        let recvd = &recv_buf[..read];
        info!("got message [{:?}] from remote = {}", recvd, remote_addr);

        assert_eq!(
            socket
                .send_to_vectored(
                    &[IoSlice::new(RESPONSE_PREFIX.as_bytes()), IoSlice::new(recvd)],
                    remote_addr
                )
                .await
                .expect("send to remote"),
            recvd.len() + RESPONSE_PREFIX.len()
        );
    }
}
