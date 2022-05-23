// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Constants and helper methods shared between the client and server in the
//! configurable netstack integration test.

use net_declare::std_socket_addr;

pub const BUS_NAME: &str = "test-bus";
pub const SERVER_NAME: &str = "server";
pub const REQUEST: &str = "hello from client";
pub const RESPONSE: &str = "hello from server";

// These should match the static IP addresses assigned to the 'server-ep'
// interface in the configurable-netstack-test.cml manifest.
pub fn server_ips() -> [std::net::SocketAddr; 2] {
    [std_socket_addr!("192.168.0.1:8080"), std_socket_addr!("192.168.0.3:8080")]
}
