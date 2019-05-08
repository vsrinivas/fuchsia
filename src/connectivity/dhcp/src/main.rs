// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![deny(warnings)]

use {
    dhcp::{
        configuration,
        protocol::{Message, SERVER_PORT},
        server::Server,
    },
    failure::{Error, Fail, ResultExt},
    fuchsia_async::{net::UdpSocket, Executor, Interval},
    fuchsia_syslog::{self as fx_log, fx_log_info, fx_vlog},
    fuchsia_zircon::{self as zx, DurationNum},
    futures::{future::try_join, Future, StreamExt, TryStreamExt},
    getopts::Options,
    std::{
        env,
        net::{IpAddr, Ipv4Addr, SocketAddr},
        sync::Mutex,
    },
    void::Void,
};

/// A buffer size in excess of the maximum allowable DHCP message size.
const BUF_SZ: usize = 1024;
const DEFAULT_CONFIG_PATH: &str = "/pkg/data/config.json";
/// The rate in seconds at which expiration DHCP leases are recycled back into the managed address
/// pool. The current value of 5 is meant to facilitate manual testing.
// TODO(atait): Replace with Duration type after it has been updated to const fn.
const EXPIRATION_INTERVAL_SECS: i64 = 5;

fn main() -> Result<(), Error> {
    fx_log::init()?;
    fx_log::set_severity(fx_log::levels::INFO);

    let mut exec = Executor::new().context("error creating executor")?;
    let path = get_server_config_file_path()?;
    let config = configuration::load_server_config_from_file(path)?;
    let server_ip = config.server_ip;
    let socket_addr = SocketAddr::new(IpAddr::V4(server_ip), SERVER_PORT);
    let udp_socket = UdpSocket::bind(&socket_addr).context("unable to bind socket")?;
    udp_socket.set_broadcast(true).context("unable to set broadcast")?;
    let server = Mutex::new(Server::from_config(config, || {
        zx::Time::get(zx::ClockId::UTC).into_nanos() / 1_000_000_000
    }));
    let msg_handling_loop = define_msg_handling_loop_future(udp_socket, &server);
    let lease_expiration_handler = define_lease_expiration_handler_future(&server);

    fx_log_info!(tag: "dhcpd", "starting server");
    exec.run_singlethreaded(try_join(msg_handling_loop, lease_expiration_handler))
        .map_err(|e| e.context("failed to start event loop"))?;
    fx_log_info!(tag: "dhcpd", "server shutting down");
    Ok(())
}

fn get_server_config_file_path() -> Result<String, Error> {
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

async fn define_msg_handling_loop_future<F: Fn() -> i64>(
    sock: UdpSocket, server: &Mutex<Server<F>>,
) -> Result<Void, Error> {
    let mut buf = vec![0u8; BUF_SZ];
    loop {
        let (received, mut sender) = await!(sock.recv_from(&mut *buf))
            .map_err(|_e| failure::err_msg("unable to receive buffer"))?;
        fx_log_info!(tag: "dhcpd", "received {} bytes", received);
        let msg = Message::from_buffer(&buf[0..received])
            .ok_or_else(|| failure::err_msg("unable to parse buffer"))?;
        fx_vlog!(tag: "dhcpd", 1, "msg parsed {:?}", msg);
        // This call should not block because the server is single-threaded.
        let response = server
            .lock()
            .unwrap()
            .dispatch(msg)
            .ok_or_else(|| failure::err_msg("invalid message"))?;
        fx_vlog!(tag: "dhcpd", 1, "msg dispatched to server {:?}", response);
        let response_buffer = response.serialize();
        // A new DHCP client sending a DHCPDISCOVER message will send
        // it from 0.0.0.0. In order to respond with a DHCPOFFER, the server
        // must broadcast the response. See RFC 2131 Section 4.4.1 for further
        // details.
        if sender.ip() == IpAddr::V4(Ipv4Addr::UNSPECIFIED) {
            sender.set_ip(IpAddr::V4(Ipv4Addr::BROADCAST));
        }
        await!(sock.send_to(&response_buffer, sender)).context("unable to send response")?;
        fx_log_info!(tag: "dhcpd", "response sent");
    }
}

fn define_lease_expiration_handler_future<'a, F: Fn() -> i64>(
    server: &'a Mutex<Server<F>>,
) -> impl Future<Output = Result<(), Error>> + 'a {
    let expiration_interval = Interval::new(EXPIRATION_INTERVAL_SECS.seconds());
    expiration_interval
        .map(move |()| {
            server.lock().unwrap().release_expired_leases();
        })
        .map(|_| Ok(()))
        .try_collect::<()>()
}
