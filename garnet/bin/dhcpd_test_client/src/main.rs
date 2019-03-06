// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

use {
    dhcp::protocol::{ConfigOption, Message, MessageType, OptionCode, CLIENT_PORT, SERVER_PORT},
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_hardware_ethernet_ext::MacAddress as MacAddr,
    fuchsia_async::{net::UdpSocket, Executor},
    fuchsia_syslog::{self as fx_log, fx_log_info},
    std::net::{IpAddr, Ipv4Addr, SocketAddr},
};

const TEST_MAC: [u8; 6] = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];
const TEST_XID: u32 = 42;

fn main() -> Result<(), Error> {
    fx_log::init_with_tags(&["dhcpd_test_client"])?;
    let mut exec = Executor::new().context("unable to create executor")?;

    exec.run_singlethreaded(
        async {
            let sock = build_and_bind_socket().context("unable to create socket")?;
            let dst = SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), SERVER_PORT);

            // Send DHCP Discover.
            let disc = build_discover();
            fx_log_info!("preparing to send discover message: {:?}", disc);
            let serialized = disc.serialize();
            await!(sock.send_to(&serialized, dst))?;
            fx_log_info!("discover message sent");

            // Receive DHCP Offer.
            let mut buf = vec![0u8; 1024];
            let (bytes_recvd, _addr) = await!(sock.recv_from(&mut buf))?;
            let offer = match Message::from_buffer(&buf[0..bytes_recvd]) {
                Some(msg) => Ok(msg),
                None => Err(format_err!("unable to parse offer")),
            }?;
            fx_log_info!("message received: {:?}", offer);

            // Send DHCP Request.
            let req = build_request(offer).context("unable to build request")?;
            fx_log_info!("preparing to send request message: {:?}", req);
            let serialized = req.serialize();
            await!(sock.send_to(&serialized, dst))?;
            fx_log_info!("request message sent");

            // Receive DHCP Ack.
            let (bytes_recvd, _addr) = await!(sock.recv_from(&mut buf))?;
            let ack = match Message::from_buffer(&buf[0..bytes_recvd]) {
                Some(msg) => Ok(msg),
                None => Err(format_err!("unable to parse ack")),
            }?;
            fx_log_info!("message received: {:?}", ack);

            Ok::<(), Error>(())
        },
    )
}

fn build_and_bind_socket() -> Result<UdpSocket, Error> {
    let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), CLIENT_PORT);
    let udp_socket = UdpSocket::bind(&addr).context("unable to bind socket")?;
    udp_socket.set_broadcast(true).context("unable to set broadcast")?;
    Ok(udp_socket)
}

fn build_discover() -> Message {
    let mut disc = Message::new();
    disc.xid = TEST_XID;
    disc.chaddr = MacAddr { octets: TEST_MAC };
    disc.options.push(ConfigOption {
        code: OptionCode::DhcpMessageType,
        value: vec![MessageType::DHCPDISCOVER.into()],
    });
    disc
}

fn build_request(offer: Message) -> Result<Message, Error> {
    let mut req = Message::new();
    req.xid = TEST_XID;
    req.ciaddr = offer.yiaddr;
    req.chaddr = MacAddr { octets: TEST_MAC };
    req.options.push(ConfigOption {
        code: OptionCode::DhcpMessageType,
        value: vec![MessageType::DHCPREQUEST.into()],
    });
    match offer.get_config_option(OptionCode::ServerId) {
        Some(id) => {
            req.options.push(id.clone());
            Ok(req)
        }
        None => Err(failure::err_msg("unable to get server id")),
    }
}
