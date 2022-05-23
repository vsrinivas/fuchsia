// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use configurable_netstack_test::{server_ips, BUS_NAME, REQUEST, RESPONSE, SERVER_NAME};
use log::info;
use std::io::{Read as _, Write as _};

#[fuchsia_async::run_singlethreaded]
async fn main() {
    diagnostics_log::init!();

    let listeners = server_ips()
        .iter()
        .map(|addr| std::net::TcpListener::bind(&addr).expect(&format!("bind to address {}", addr)))
        .collect::<Vec<_>>();
    info!("waiting for connections...");
    // Let the client know that we are listening for incoming connections.
    let _bus = netemul_sync::Bus::subscribe(BUS_NAME, SERVER_NAME).expect("subscribe to bus");

    for listener in listeners {
        let (mut stream, remote) = listener.accept().expect("accept incoming connection");
        info!("accepted connection from {}", remote);
        let mut buffer = [0; 512];
        let read = stream.read(&mut buffer).expect("read from socket");
        let request = String::from_utf8_lossy(&buffer[0..read]);
        assert_eq!(request, REQUEST, "got unexpected request from client: {}", request);

        info!("got request: '{}'", request);
        let response = RESPONSE.as_bytes();
        assert_eq!(stream.write(response).expect("write to socket"), response.len());
        stream.flush().expect("flush stream");
    }
}
