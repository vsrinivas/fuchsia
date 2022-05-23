// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::io::{Read as _, Write as _};

const BUS_NAME: &str = "test-bus";
const CLIENT_NAME: &str = "client";
const SERVER_NAME: &str = "server";
const REQUEST: &str = "hello from client";
const RESPONSE: &str = "hello from server";

#[fuchsia::test]
async fn connect_to_server() {
    netemul_sync::Bus::subscribe(BUS_NAME, CLIENT_NAME)
        .expect("subscribe to bus")
        .wait_for_client(SERVER_NAME)
        .await
        .expect("wait for server to join bus");

    let mut stream = std::net::TcpStream::connect("192.168.0.2:8080").expect("connect to server");
    let request = REQUEST.as_bytes();
    assert_eq!(stream.write(request).expect("write to socket"), request.len());
    stream.flush().expect("flush stream");

    let mut buffer = [0; 512];
    let read = stream.read(&mut buffer).expect("read from socket");
    let response = String::from_utf8_lossy(&buffer[0..read]);
    assert_eq!(response, RESPONSE, "got unexpected response from server: {}", response);
}
