// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ServerEnd;
use fidl_fuchsia_net_icmp::{EchoSocketConfig, EchoSocketMarker, ProviderRequest};

/// Handle a fuchsia.net.icmp.Provider FIDL request, which are used for opening ICMP sockets.
pub async fn handle_request(req: ProviderRequest) {
    match req {
        ProviderRequest::OpenEchoSocket { config, socket, control_handle } => {
            open_echo_socket(config, socket);
        }
    }
}

fn open_echo_socket(
    config: EchoSocketConfig,
    socket: ServerEnd<EchoSocketMarker>,
) -> Result<(), fidl::Error> {
    // TODO(sbalana): Implement opening ICMP echo sockets
    Ok(())
}

#[cfg(test)]
mod test {
    use fidl_fuchsia_net::{IpAddress, Ipv4Address};
    use fidl_fuchsia_net_icmp::{EchoSocketConfig, EchoSocketMarker};

    use crate::eventloop::integration_tests::{
        new_ipv4_addr_subnet, StackSetupBuilder, TestSetupBuilder,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket() {
        // Use ICMP Echo sockets to ping between two stacks
        const ALICE: usize = 0;
        const BOB: usize = 1;
        const ALICE_IP: [u8; 4] = [192, 168, 0, 1];
        const BOB_IP: [u8; 4] = [192, 168, 0, 2];

        let mut t = TestSetupBuilder::new()
            .add_named_endpoint("alice")
            .add_named_endpoint("bob")
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint("alice", Some(new_ipv4_addr_subnet(ALICE_IP, 24))),
            )
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint("bob", Some(new_ipv4_addr_subnet(BOB_IP, 24))),
            )
            .build()
            .await
            .expect("Test Setup succeeds");

        // Wait for interfaces on both stacks to signal online correctly
        t.get(ALICE).wait_for_interface_online(1).await;
        t.get(BOB).wait_for_interface_online(1).await;

        // Open ICMP Echo socket from Alice to Bob
        let source_stack = t.get(ALICE);
        let icmp_provider = source_stack.connect_icmp_provider().unwrap();
        let mut config = EchoSocketConfig {
            local: Some(IpAddress::Ipv4(Ipv4Address { addr: ALICE_IP })),
            remote: Some(IpAddress::Ipv4(Ipv4Address { addr: BOB_IP })),
        };

        let (socket_client, socket_server) =
            fidl::endpoints::create_endpoints::<EchoSocketMarker>().unwrap();
        let socket = socket_client.into_proxy().unwrap();
        let mut event_stream = socket.take_event_stream();

        icmp_provider.open_echo_socket(config, socket_server).expect("ICMP Echo socket opens");

        // TODO(sbalana): Wait for ICMP Echo socket to open
        // TODO(sbalana): Send ICMP Echoes from Source to Destination
    }
}
