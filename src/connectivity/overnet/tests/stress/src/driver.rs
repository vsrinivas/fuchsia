// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_overnet as fovernet, fidl_test_proxy_stress as fstress,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    futures::{AsyncReadExt, StreamExt},
    tracing::info,
};

async fn connect_to_stressor() -> fstress::StressorProxy {
    const PROTOCOL_NAME: &str = "test.proxy.stress.Stressor";
    let service_consumer_proxy =
        connect_to_protocol::<fovernet::ServiceConsumerMarker>().expect("connect to consumer");
    let mut peer_id = None;
    while peer_id.is_none() {
        info!("Trying to get peer");
        let peers = service_consumer_proxy.list_peers().await.expect("list peers");
        peer_id = peers
            .into_iter()
            .filter_map(|peer| match peer.description.services {
                None => None,
                Some(services) if services.contains(&PROTOCOL_NAME.to_string()) => Some(peer.id),
                Some(_) => None,
            })
            .next();
        info!("Got peer: {:?}", !peer_id.is_none());
    }
    let (proxy, server) = create_proxy::<fstress::StressorMarker>().expect("create proxy");
    service_consumer_proxy
        .connect_to_service(&mut peer_id.unwrap(), PROTOCOL_NAME, server.into_channel())
        .expect("connect over overnet");
    proxy
}

#[fuchsia::test]
async fn test_stuff_socket() {
    let proxy = connect_to_stressor().await;
    let (client_socket, server_socket) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create socket");
    let bytes_written = proxy.stuff_socket(server_socket).await.expect("stuff socket");

    // Full socket shouldn't block other requests.
    assert_eq!(proxy.echo("john").await.unwrap(), "john");
    // All data should be available from the socket.
    let mut async_socket =
        fuchsia_async::Socket::from_socket(client_socket).expect("create async socket");
    let mut socket_data = vec![];
    async_socket.read_to_end(&mut socket_data).await.expect("read socket");
    assert_eq!(socket_data, vec![0xFF; bytes_written as usize]);
}

#[fuchsia::test]
async fn test_parallel_connections() {
    const NUM_CONNECTIONS: usize = 100;
    const NUM_REQUESTS: usize = 100;

    let connections: Vec<_> =
        futures::stream::iter(0..NUM_CONNECTIONS).then(|_| connect_to_stressor()).collect().await;

    // call echo simultaneously on each proxy.
    let echo_futs: Vec<_> = connections.iter().map(|proxy| proxy.echo("echo")).collect();
    for echo_result in futures::future::join_all(echo_futs).await {
        assert_eq!(echo_result.unwrap(), "echo");
    }

    // call echo a few times on each proxy
    futures::stream::iter(connections)
        .for_each_concurrent(None, |proxy| async move {
            for echo_num in 0..NUM_REQUESTS {
                let echo_content = format!("echo-{:?}", echo_num);
                assert_eq!(proxy.echo(&echo_content).await.unwrap(), echo_content);
            }
        })
        .await;
}

#[fuchsia::test]
async fn test_pipelined_connections() {
    const NUM_PIPELINED: usize = 100;
    const NUM_CONNECTIONS: usize = 100;

    let connections: Vec<_> =
        futures::stream::iter(0..NUM_CONNECTIONS).then(|_| connect_to_stressor()).collect().await;

    // call echo a few times on each proxy
    futures::stream::iter(connections)
        .for_each_concurrent(None, |proxy| async move {
            let echo_futs: Vec<_> = (0..NUM_PIPELINED).map(|_| proxy.echo("echo")).collect();
            for echo_result in futures::future::join_all(echo_futs).await {
                assert_eq!(echo_result.unwrap(), "echo");
            }
        })
        .await;
}
