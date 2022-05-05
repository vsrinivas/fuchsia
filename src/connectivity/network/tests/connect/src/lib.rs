// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_netemul_network as fnetemul_network;
use fuchsia_async as fasync;
use futures_util::{AsyncReadExt as _, AsyncWriteExt as _, FutureExt as _};
use net_declare::{fidl_subnet, std_ip};
use netemul::{RealmTcpListener as _, RealmTcpStream as _};
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_macros::variants_test;
use tcp_stream_ext::TcpStreamExt as _;

async fn measure(fut: impl std::future::Future<Output = ()>) -> std::time::Duration {
    let start = std::time::Instant::now();
    let () = fut.await;
    start.elapsed()
}

async fn verify_error(mut stream: fasync::net::TcpStream) {
    let mut buf = [0xad; 1];
    {
        let expected = std::io::ErrorKind::TimedOut;
        match stream.read(&mut buf).await {
            Ok(n) => panic!("read {} bytes, expected {:?}", n, expected),
            Err(io_error) => {
                if io_error.kind() != expected {
                    panic!("unexpected IO error; expected {:?}, got {:?}", expected, io_error)
                }
            }
        };
    }
    // The first read consumes the error.
    let n = stream.read(&mut buf).await.expect("read after error");
    if n != 0 {
        panic!("read {}/{} bytes", n, 0);
    }
    {
        let expected = std::io::ErrorKind::BrokenPipe;
        match stream.write(&buf).await {
            Ok(n) => panic!("wrote {} bytes, expected {:?}", n, expected),
            Err(io_error) => {
                if io_error.kind() != expected {
                    panic!("unexpected IO error; expected {:?}, got {:?}", expected, io_error)
                }
            }
        };
    }
}

const SERVER_IP: fnet::Subnet = fidl_subnet!("192.168.0.1/24");
const CLIENT_IP: fnet::Subnet = fidl_subnet!("192.168.0.2/24");
const REMOTE_IP: std::net::IpAddr = std_ip!("192.168.0.1");
const PORT: u16 = 80;

#[variants_test]
async fn timeouts<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let network = sandbox.create_network("net").await.expect("create network");
    let client = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_client", name))
        .expect("create realm");
    let server = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_server", name))
        .expect("create realm");
    let client_iface = client
        .join_network::<E, _>(&network, "client-ep")
        .await
        .expect("install interface in client netstack");
    client_iface.add_address_and_subnet_route(CLIENT_IP).await.expect("configure address");
    let server_iface = server
        .join_network::<E, _>(&network, "server-ep")
        .await
        .expect("install interface in server netstack");
    server_iface.add_address_and_subnet_route(SERVER_IP).await.expect("configure address");

    let _server_sock = fasync::net::TcpListener::listen_in_realm(
        &server,
        std::net::SocketAddr::from((std::net::Ipv4Addr::UNSPECIFIED, PORT)),
    )
    .await
    .expect("failed to create server socket");

    let sockaddr = std::net::SocketAddr::from((REMOTE_IP, PORT));

    let keepalive_timeout = fasync::net::TcpStream::connect_in_realm(&client, sockaddr)
        .await
        .expect("create client socket");
    let keepalive_usertimeout = fasync::net::TcpStream::connect_in_realm(&client, sockaddr)
        .await
        .expect("create client socket");
    let mut retransmit_timeout = fasync::net::TcpStream::connect_in_realm(&client, sockaddr)
        .await
        .expect("create client socket");
    let mut retransmit_usertimeout = fasync::net::TcpStream::connect_in_realm(&client, sockaddr)
        .await
        .expect("create client socket");

    // Now that we have our connections, partition the network.
    network
        .set_config(fnetemul_network::NetworkConfig {
            latency: None,
            packet_loss: Some(fnetemul_network::LossConfig::RandomRate(100)),
            reorder: None,
            ..fnetemul_network::NetworkConfig::EMPTY
        })
        .await
        .expect("call set config");

    let connect_timeout = fasync::net::TcpStream::connect_in_realm(&client, sockaddr);
    let connect_timeout = async {
        match connect_timeout.await {
            Ok(stream) => {
                let _: fuchsia_async::net::TcpStream = stream;
                panic!("unexpectedly connected")
            }
            Err(e) => {
                // Verify that we got `ETIMEDOUT` or `EHOSTUNREACH`.
                let io_error =
                    e.downcast::<std::io::Error>().expect("underlying error should be an IO error");
                let raw_error = io_error.raw_os_error().expect("extract raw OS error");
                assert!(
                    raw_error == libc::ETIMEDOUT || raw_error == libc::EHOSTUNREACH,
                    "unexpected IO error: {}",
                    io_error
                );
            }
        }
    };

    const USER_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);
    for socket in [&keepalive_usertimeout, &retransmit_usertimeout].iter() {
        socket.std().set_user_timeout(USER_TIMEOUT).expect("set TCP user timeout option");
    }

    // Start the keepalive machinery.
    //
    // [`socket::TcpKeepalive::with_time`] sets TCP_KEEPIDLE, which requires a
    // minimum of 1 second.
    let keepalive = socket2::TcpKeepalive::new().with_time(std::time::Duration::from_secs(1));
    socket2::SockRef::from(keepalive_usertimeout.std())
        .set_tcp_keepalive(&keepalive)
        .expect("set TCP keepalive option");
    let keepalive = keepalive.with_interval(std::time::Duration::from_secs(1)).with_retries(1);
    socket2::SockRef::from(keepalive_timeout.std())
        .set_tcp_keepalive(&keepalive)
        .expect("set TCP keepalive option");

    // Start the retransmit machinery.
    for socket in [&mut retransmit_timeout, &mut retransmit_usertimeout].iter_mut() {
        socket.write_all(&[0xde]).await.expect("write to socket");
    }

    let connect_timeout = measure(connect_timeout).fuse();
    let keepalive_timeout = measure(verify_error(keepalive_timeout)).fuse();
    let keepalive_usertimeout = measure(verify_error(keepalive_usertimeout)).fuse();
    let retransmit_timeout = measure(verify_error(retransmit_timeout)).fuse();
    let retransmit_usertimeout = measure(verify_error(retransmit_usertimeout)).fuse();

    futures_util::pin_mut!(
        connect_timeout,
        keepalive_timeout,
        keepalive_usertimeout,
        retransmit_timeout,
        retransmit_usertimeout,
    );

    macro_rules! print_elapsed {
        ($val:expr) => {
            println!("{} timed out after {:?}", stringify!($val), $val);
        };
    }

    // TODO(https://fxbug.dev/52278): Enable retransmit timeout test, after we are
    // able to tune the TCP stack to reduce this time. Currently it is too long for
    // the test.
    let _ = retransmit_timeout;

    loop {
        futures_util::select! {
          connect = connect_timeout => {
            print_elapsed!(connect);
          },
          keepalive = keepalive_timeout => {
            print_elapsed!(keepalive);
          },
          keepalive_user = keepalive_usertimeout => {
            print_elapsed!(keepalive_user);
          },
          retransmit_user = retransmit_usertimeout => {
            print_elapsed!(retransmit_user);
          },
          complete => break,
        }
    }
}
