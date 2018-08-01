// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

extern crate dhcp;
extern crate failure;
extern crate fuchsia_async as async;
extern crate futures;
extern crate getopts;

use async::Executor;
use async::net::UdpSocket;
use dhcp::configuration;
use dhcp::protocol::{Message, SERVER_PORT};
use dhcp::server::Server;
use failure::{Error, Fail, ResultExt};
use futures::future::{self, Loop};
use futures::prelude::*;
use getopts::Options;
use std::env;
use std::io;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::sync::{Arc, Mutex};

/// A buffer size in excess of the maximum allowable DHCP message size.
const BUF_SZ: usize = 1024;
const DEFAULT_CONFIG_PATH: &str = "/pkg/data/config.json";

fn main() -> Result<(), Error> {
    println!("dhcpd: starting...");
    let mut exec = Executor::new().context("error creating executor")?;
    let config_path = get_config_path()?;
    let (server, server_ip) = build_server(config_path)?;
    let udp_socket = bind_server_socket(server_ip)?;
    build_and_run_event_loop(&mut exec, udp_socket, server)
}

fn get_config_path() -> Result<String, Error> {
    let args: Vec<String> = env::args().collect();
    let program = &args[0];
    let mut opts = Options::new();
    opts.optopt("c", "config", "dhcpd configuration file path", "FILE");
    let matches = match opts.parse(args[1..].iter()) {
        Ok(m) => m,
        Err(e) => {
            opts.short_usage(program);
            return Err(e.context("failed to parse options").into());
        }
    };
    match matches.opt_str("c") {
        Some(p) => Ok(p),
        None => Ok(DEFAULT_CONFIG_PATH.to_string()),
    }
}

fn build_server(path: String) -> Result<(Arc<Mutex<Server>>, Ipv4Addr), Error> {
    let config = configuration::load_server_config_from_file(path)?;
    let server_ip = config.server_ip;
    Ok((Arc::new(Mutex::new(Server::from_config(config))), server_ip))
}

fn bind_server_socket(server_ip: Ipv4Addr) -> Result<UdpSocket, Error> {
    let addr = SocketAddr::new(IpAddr::V4(server_ip), SERVER_PORT);
    let udp_socket = UdpSocket::bind(&addr).context("unable to bind socket")?;
    Ok(udp_socket)
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
