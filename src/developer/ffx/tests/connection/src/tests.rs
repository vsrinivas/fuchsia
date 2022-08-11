// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::util::{setup_and_teardown_fixture, LaunchedComponentConnector},
    fixture::fixture,
    futures::{AsyncReadExt, StreamExt, TryFutureExt},
};

mod util;

#[fixture(setup_and_teardown_fixture)]
#[fuchsia::test]
async fn test_stuff_socket(connector: LaunchedComponentConnector) {
    let proxy = connector.connect().await.expect("connect to stressor");
    let (client_socket, server_socket) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
    let bytes_written_fut = proxy.stuff_socket(server_socket);
    let bytes_written = bytes_written_fut.await.expect("stuff socket");

    // Full socket shouldn't block other requests.
    assert_eq!(proxy.echo("john").await.unwrap(), "john");
    // All data should be available from the socket.
    let mut async_socket =
        fuchsia_async::Socket::from_socket(client_socket).expect("create async socket");
    let mut socket_data = vec![];
    async_socket.read_to_end(&mut socket_data).await.expect("read socket");
    assert_eq!(socket_data, vec![0xFF; bytes_written as usize]);
}

#[ignore]
#[fixture(setup_and_teardown_fixture)]
#[fuchsia::test]
async fn test_parallel_connections(connector: LaunchedComponentConnector) {
    const NUM_CONNECTIONS: usize = 100;
    const NUM_REQUESTS: usize = 100;

    let connections: Vec<_> = futures::stream::iter(0..NUM_CONNECTIONS)
        .then(|_| connector.connect().unwrap_or_else(|e| panic!("{}", e)))
        .collect()
        .await;

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

#[ignore]
#[fixture(setup_and_teardown_fixture)]
#[fuchsia::test]
async fn test_pipelined_connections(connector: LaunchedComponentConnector) {
    const NUM_PIPELINED: usize = 100;
    const NUM_CONNECTIONS: usize = 100;

    let connections: Vec<_> = futures::stream::iter(0..NUM_CONNECTIONS)
        .then(|_| connector.connect().unwrap_or_else(|e| panic!("{}", e)))
        .collect()
        .await;

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

#[ignore] // This test currently fails.
#[fixture(setup_and_teardown_fixture)]
#[fuchsia::test]
async fn test_parallel_connections_multiple_connections_to_daemon(
    connector: LaunchedComponentConnector,
) {
    const NUM_CONNECTIONS: usize = 100;
    const NUM_REQUESTS: usize = 100;

    let connections: Vec<_> = futures::stream::iter(0..NUM_CONNECTIONS)
        .then(|_| async { connector.connect_via_new_daemon_connection().await.expect("new rcs") })
        .collect()
        .await;

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
