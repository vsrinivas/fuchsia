// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Constants and helper methods shared between the client and server in the
//! configurable netstack integration test.

use fidl_fuchsia_netemul_sync as fnetemul_sync;
use net_declare::std_socket_addr;

pub const CLIENT_NAME: &str = "client";
pub const SERVER_NAME: &str = "server";
pub const REQUEST: &str = "hello from client";
pub const RESPONSE: &str = "hello from server";

pub fn server_ips() -> [std::net::SocketAddr; 2] {
    [std_socket_addr!("192.168.0.1:8080"), std_socket_addr!("192.168.0.3:8080")]
}

const BUS_NAME: &str = "test-bus";

pub struct Bus {
    bus: fnetemul_sync::BusProxy,
}

impl Bus {
    pub fn subscribe(client: &str) -> Self {
        let sync_manager =
            fuchsia_component::client::connect_to_protocol::<fnetemul_sync::SyncManagerMarker>()
                .expect("connect to protocol");
        let (bus, server_end) =
            fidl::endpoints::create_proxy::<fnetemul_sync::BusMarker>().expect("create proxy");
        sync_manager.bus_subscribe(BUS_NAME, client, server_end).expect("subscribe to bus");
        Bus { bus }
    }

    pub async fn wait_for_client(&self, client: &str) {
        let (success, absent) = self
            .bus
            .wait_for_clients(&mut std::iter::once(client), /* no timeout */ 0)
            .await
            .expect("wait for client to join bus");
        assert!(success, "clients still not present on bus: {:?}", absent);
    }
}
