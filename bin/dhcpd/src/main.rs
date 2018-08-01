// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

extern crate dhcp;
extern crate failure;
extern crate fuchsia_async as async;
extern crate futures;

use async::Executor;
use async::net::UdpSocket;
use failure::{Error, Fail, ResultExt};
use futures::future::{self, Loop};
use futures::prelude::*;
use dhcp::protocol::{Message, SERVER_PORT};
use dhcp::server::{Server, ServerConfig};
use std::io;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::sync::{Arc, Mutex};

/// A buffer size in excess of the maximum allowable DHCP message size.
const BUF_SZ: usize = 1024;

fn main() -> Result<(), Error> {
    println!("dhcpd: starting...");
    let mut exec = Executor::new().context("error creating executor")?;
    let (udp_socket, server) = setup_and_bind_server(Ipv4Addr::new(127, 0, 0, 1))?;
    build_and_run_event_loop(&mut exec, udp_socket, server)
}

fn setup_and_bind_server(server_ip: Ipv4Addr) -> Result<(UdpSocket, Arc<Mutex<Server>>), Error> {
    let addr = SocketAddr::new(IpAddr::V4(server_ip), SERVER_PORT);
    let udp_socket = UdpSocket::bind(&addr).context("unable to bind socket")?;
    let server = build_server(server_ip)?;
    Ok((udp_socket, server))
}

fn build_server(server_ip: Ipv4Addr) -> Result<Arc<Mutex<Server>>, Error> {
    let mut server = Server::new();
    // Placeholder addresses until the server supports loading addresses
    // from a configuration file.
    let addrs = vec![Ipv4Addr::new(192, 168, 0, 2),
                     Ipv4Addr::new(192, 168, 0, 3),
                     Ipv4Addr::new(192, 168, 0, 4)];
    let config = ServerConfig {
        server_ip: server_ip,
        default_lease_time: 0,
        subnet_mask: 24,
    };
    server.add_addrs(addrs);
    server.set_config(config);
    Ok(Arc::new(Mutex::new(server)))
}

fn build_and_run_event_loop(exec: &mut Executor, sock: UdpSocket, server: Arc<Mutex<Server>>) -> Result<(), Error> {
    let event_loop = future::loop_fn(sock, |sock| {
        // Cloning the server ARC and then moving the value into the first and_then() call
        // allows the server value to live multiple loop iterations.
        let server = server.clone();
        let buf = vec![0u8; BUF_SZ];
        sock.recv_from(buf)
        .map_err(|_e| {
            failure::err_msg("dhcpd: unable to receive buffer")
        }).and_then(move |(sock, buf, received, addr)| {
            println!("dhcpd: received {} bytes", received);
            match Message::from_buffer(&buf) {
                None => Err(failure::err_msg("dhcpd: unable to parse buffer")),
                Some(msg) => {
                    println!("dhcpd: msg parsed: {:?}", msg);
                    // This call should not block because the server is single-threaded.
                    match server.lock().unwrap().dispatch(msg) {
                        None => Err(failure::err_msg("dhcpd: invalid Message")),
                        Some(response) => {
                            println!("dhcpd: msg dispatched to server {:?}", response);
                            let response_buffer = response.serialize();
                            println!("dhcpd: response serialized");
                            return Ok((sock, response_buffer, addr));
                        }
                    }
                }
            }
        }).map_err(|e| {
            io::Error::new(io::ErrorKind::InvalidData, e.compat())
        }).and_then(|result| {
            let (sock, buf, addr) = result;
            sock.send_to(buf, addr)
        }).map_err(|e| {
            e.context("dhcpd: unable to send response")
        }).and_then(|sock| {
            println!("dhcpd: response sent. Continuing event loop.");
            Ok(Loop::Continue(sock))
        })
    });

    println!("dhcpd: starting event loop...");
    exec.run_singlethreaded(event_loop).context("could not run futures")?;
    println!("dhcpd: shutting down...");
    Ok(())
}
