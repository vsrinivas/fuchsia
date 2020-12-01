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
    use crate::Executor;
    use std::io::Error;

    #[test]
    fn send_recv() {
        let mut exec = Executor::new().expect("could not create executor");

        let addr = "127.0.0.1:29995".parse().expect("could not parse test address");
        let buf = b"hello world";
        let socket = UdpSocket::bind(&addr).expect("could not create socket");
        let fut = async move {
            assert_eq!(buf.len(), socket.send_to(buf, addr).await?);
            let mut recvbuf = vec![0; 11];
            let (received, sender) = socket.recv_from(&mut *recvbuf).await?;
            assert_eq!(addr, sender);
            assert_eq!(received, buf.len());
            assert_eq!(&*buf, &*recvbuf);
            Ok::<(), Error>(())
        };

        exec.run_singlethreaded(fut).expect("failed to run udp socket test");
    }

    #[test]
    fn broadcast() {
        let mut _exec = Executor::new().expect("could not create executor");

        let addr = "127.0.0.1:12345".parse().expect("could not parse test address");
        let socket = UdpSocket::bind(&addr).expect("could not create socket");
        let initial = socket.broadcast().expect("could not get broadcast");
        assert!(!initial);
        socket.set_broadcast(true).expect("could not set broadcast");
        let set = socket.broadcast().expect("could not get broadcast");
        assert!(set);
    }

    #[test]
    fn test_local_addr() {
        let mut _exec = Executor::new().expect("could not create executor");
        let addr = "127.0.0.1:5432".parse().expect("could not parse test address");
        let socket = UdpSocket::bind(&addr).expect("could not create socket");
        assert_eq!(socket.local_addr().expect("could not get local address"), addr);
    }
}
