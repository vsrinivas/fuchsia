// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
mod fuchsia;

#[cfg(target_os = "fuchsia")]
pub use fuchsia::*;

#[cfg(all(not(target_os = "fuchsia"), not(target_arch = "wasm32")))]
mod portable;

#[cfg(all(not(target_os = "fuchsia"), not(target_arch = "wasm32")))]
pub use portable::*;

#[cfg(not(target_arch = "wasm32"))]
#[cfg(test)]
mod udp_tests {
    use super::UdpSocket;
    use crate::TestExecutor;

    const MESSAGE: &[u8; 11] = b"hello world";

    fn send_recv_same_socket(addr: std::net::IpAddr) {
        let mut exec = TestExecutor::new().expect("could not create executor");

        let addr = std::net::SocketAddr::new(addr, 0);
        let socket = UdpSocket::bind(&addr).expect("could not create socket");
        let addr = socket.local_addr().expect("could not get local address");
        let fut = async move {
            assert_eq!(socket.send_to(MESSAGE, addr).await.expect("send_to failed"), MESSAGE.len());
            let mut recvbuf = [0; MESSAGE.len()];
            assert_eq!(
                socket.recv_from(&mut recvbuf).await.expect("recv_from failed"),
                (MESSAGE.len(), addr)
            );
            assert_eq!(recvbuf, *MESSAGE);
        };

        exec.run_singlethreaded(fut);
    }

    #[test]
    fn send_recv_same_socket_ipv4() {
        send_recv_same_socket(std::net::IpAddr::V4(std::net::Ipv4Addr::LOCALHOST))
    }

    #[test]
    fn send_recv_same_socket_ipv6() {
        send_recv_same_socket(std::net::IpAddr::V6(std::net::Ipv6Addr::LOCALHOST))
    }

    fn send_recv(addr: std::net::IpAddr) {
        let mut exec = TestExecutor::new().expect("could not create executor");

        let socket_addr = std::net::SocketAddr::new(addr.into(), 0);
        let client_socket = UdpSocket::bind(&socket_addr).expect("could not create client socket");
        let server_socket = UdpSocket::bind(&socket_addr).expect("could not create server socket");
        let client_addr =
            client_socket.local_addr().expect("could not get client socket's local address");
        let server_addr =
            server_socket.local_addr().expect("could not get server socket's local address");
        let fut = async move {
            assert_eq!(
                client_socket.send_to(MESSAGE, server_addr).await.expect("send_to failed"),
                MESSAGE.len()
            );
            let mut recvbuf = [0; MESSAGE.len()];
            assert_eq!(
                server_socket.recv_from(&mut recvbuf).await.expect("recv_from failed"),
                (MESSAGE.len(), client_addr)
            );
            assert_eq!(*MESSAGE, recvbuf);
        };

        exec.run_singlethreaded(fut);
    }

    #[test]
    fn send_recv_ipv4() {
        send_recv(std::net::IpAddr::V4(std::net::Ipv4Addr::LOCALHOST))
    }

    #[test]
    fn send_recv_ipv6() {
        send_recv(std::net::IpAddr::V6(std::net::Ipv6Addr::LOCALHOST))
    }

    #[test]
    fn broadcast() {
        let mut _exec = TestExecutor::new().expect("could not create executor");

        let addr = "127.0.0.1:0".parse().expect("could not parse test address");
        let socket = UdpSocket::bind(&addr).expect("could not create socket");
        let initial = socket.broadcast().expect("could not get broadcast");
        assert!(!initial);
        socket.set_broadcast(true).expect("could not set broadcast");
        let set = socket.broadcast().expect("could not get broadcast");
        assert!(set);
    }

    #[test]
    fn test_local_addr() {
        let mut _exec = TestExecutor::new().expect("could not create executor");
        let addr = "127.0.0.1:5432".parse().expect("could not parse test address");
        let socket = UdpSocket::bind(&addr).expect("could not create socket");
        assert_eq!(socket.local_addr().expect("could not get local address"), addr);
    }
}
