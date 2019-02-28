// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

use {
    dhcp::protocol::{ConfigOption, Message, MessageType, OptionCode, CLIENT_PORT, SERVER_PORT},
    failure::{Error, ResultExt},
    fidl_fuchsia_hardware_ethernet_ext::MacAddress as MacAddr,
    fuchsia_async::{net::UdpSocket, Executor},
    std::net::SocketAddr,
};

const TEST_MAC: [u8; 6] = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];

fn main() -> Result<(), Error> {
    println!("fake_client: starting...");
    let mut exec = Executor::new().context("error creating executor")?;
    let (sock, server) = build_and_bind_socket();

    let disc = build_discover();
    println!("Sending discover message: {:?}", disc);
    let send_msgs = async move {
        let serialized = disc.serialize();
        await!(sock.send_to(&serialized, server))?;
        let mut buf = vec![0u8; 1024];
        let (bytes_recvd, _addr) = await!(sock.recv_from(&mut buf))?;
        let offer = Message::from_buffer(&buf[0..bytes_recvd]).unwrap();
        println!("fake_client: msg rcvd {:?}", offer);
        let req = build_request(offer);
        println!("fake_client: sending request msg {:?}", req);
        let serialized = req.serialize();
        await!(sock.send_to(&serialized, server))?;
        let (bytes_recvd, _addr) = await!(sock.recv_from(&mut buf))?;
        let ack = Message::from_buffer(&buf[0..bytes_recvd]).unwrap();
        println!("fake_client: msg rcvd {:?}", ack);
        Ok::<(), Error>(())
    };

    println!("fake_client: sending messages...");
    exec.run_singlethreaded(send_msgs)
        .context("could not run futures")?;
    println!("fake_client: messages sent...");

    Ok(())
}

fn build_and_bind_socket() -> (UdpSocket, SocketAddr) {
    let addr = SocketAddr::new("127.0.0.1".parse().unwrap(), CLIENT_PORT);
    let server = SocketAddr::new("127.0.0.1".parse().unwrap(), SERVER_PORT);
    let udp_socket = UdpSocket::bind(&addr)
        .context("error binding socket")
        .unwrap();
    (udp_socket, server)
}

fn build_discover() -> Message {
    let mut disc = Message::new();
    disc.xid = 42;
    disc.chaddr = MacAddr { octets: TEST_MAC };
    disc.options.push(ConfigOption {
        code: OptionCode::DhcpMessageType,
        value: vec![MessageType::DHCPDISCOVER.into()],
    });
    disc
}

fn build_request(offer: Message) -> Message {
    let mut req = Message::new();
    req.xid = 42;
    req.ciaddr = offer.yiaddr;
    req.chaddr = MacAddr { octets: TEST_MAC };
    req.options.push(ConfigOption {
        code: OptionCode::DhcpMessageType,
        value: vec![MessageType::DHCPREQUEST.into()],
    });
    let server_id = offer
        .get_config_option(OptionCode::ServerId)
        .unwrap()
        .clone();
    req.options.push(server_id);
    req
}
