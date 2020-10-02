// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fuchsia_async::TimeoutExt as _;

use anyhow::Context as _;
use futures::{
    io::AsyncReadExt as _, io::AsyncWriteExt as _, FutureExt as _, TryFutureExt as _,
    TryStreamExt as _,
};
use net_declare::{fidl_ip_v4, fidl_ip_v6, fidl_subnet};
use netemul::{EnvironmentTcpListener as _, EnvironmentTcpStream as _, EnvironmentUdpSocket as _};
use netstack_testing_macros::variants_test;
use packet::Serializer;
use packet_formats;
use packet_formats::ipv4::Ipv4Header;

use crate::environments::{Netstack2, TestSandboxExt as _};
use crate::Result;

async fn run_udp_socket_test(
    server: &netemul::TestEnvironment<'_>,
    server_addr: fidl_fuchsia_net::IpAddress,
    client: &netemul::TestEnvironment<'_>,
    client_addr: fidl_fuchsia_net::IpAddress,
) -> Result {
    let fidl_fuchsia_net_ext::IpAddress(client_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(client_addr);
    let client_addr = std::net::SocketAddr::new(client_addr, 1234);

    let fidl_fuchsia_net_ext::IpAddress(server_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(server_addr);
    let server_addr = std::net::SocketAddr::new(server_addr, 8080);

    let client_sock = fuchsia_async::net::UdpSocket::bind_in_env(client, client_addr)
        .await
        .context("failed to create client socket")?;

    let server_sock = fuchsia_async::net::UdpSocket::bind_in_env(server, server_addr)
        .await
        .context("failed to create server socket")?;

    const PAYLOAD: &'static str = "Hello World";

    let client_fut = async move {
        let r =
            client_sock.send_to(PAYLOAD.as_bytes(), server_addr).await.context("sendto failed")?;
        assert_eq!(r, PAYLOAD.as_bytes().len());
        Result::Ok(())
    };
    let server_fut = async move {
        let mut buf = [0u8; 1024];
        let (r, from) = server_sock.recv_from(&mut buf[..]).await.context("recvfrom failed")?;
        assert_eq!(r, PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..r], PAYLOAD.as_bytes());
        assert_eq!(from, client_addr);
        Result::Ok(())
    };

    let ((), ()) = futures::future::try_join(client_fut, server_fut).await?;
    Ok(())
}

#[variants_test]
async fn test_udp_socket<E: netemul::Endpoint>(name: &str) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let net = sandbox.create_network("net").await.context("failed to create network")?;

    let client = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_client", name))
        .context("failed to create client environment")?;

    const CLIENT_SUBNET: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.2/24);
    const SERVER_SUBNET: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.1/24);

    let _client_ep = client
        .join_network::<E, _>(&net, "client", netemul::InterfaceConfig::StaticIp(CLIENT_SUBNET))
        .await
        .context("client failed to join network")?;
    let server = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_server", name))
        .context("failed to create server environment")?;
    let _server_ep = server
        .join_network::<E, _>(&net, "server", netemul::InterfaceConfig::StaticIp(SERVER_SUBNET))
        .await
        .context("server failed to join network")?;

    run_udp_socket_test(&server, SERVER_SUBNET.addr, &client, CLIENT_SUBNET.addr).await
}

async fn run_tcp_socket_test(
    server: &netemul::TestEnvironment<'_>,
    server_addr: fidl_fuchsia_net::IpAddress,
    client: &netemul::TestEnvironment<'_>,
    client_addr: fidl_fuchsia_net::IpAddress,
) -> Result {
    let fidl_fuchsia_net_ext::IpAddress(client_addr) = client_addr.into();
    let client_addr = std::net::SocketAddr::new(client_addr, 1234);

    let fidl_fuchsia_net_ext::IpAddress(server_addr) = server_addr.into();
    let server_addr = std::net::SocketAddr::new(server_addr, 8080);

    // We pick a payload that is small enough to be guaranteed to fit in a TCP segment so both the
    // client and server can read the entire payload in a single `read`.
    const PAYLOAD: &'static str = "Hello World";

    let listener = fuchsia_async::net::TcpListener::listen_in_env(server, server_addr)
        .await
        .context("failed to create server socket")?;

    let server_fut = async {
        let (_, mut stream, from) = listener.accept().await.context("accept failed")?;

        let mut buf = [0u8; 1024];
        let read_count =
            stream.read(&mut buf).await.context("read from tcp server stream failed")?;

        assert_eq!(from.ip(), client_addr.ip());
        assert_eq!(read_count, PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..read_count], PAYLOAD.as_bytes());

        let write_count =
            stream.write(PAYLOAD.as_bytes()).await.context("write to tcp server stream failed")?;
        assert_eq!(write_count, PAYLOAD.as_bytes().len());

        Result::Ok(())
    };

    let client_fut = async {
        let mut stream = fuchsia_async::net::TcpStream::connect_in_env(client, server_addr)
            .await
            .context("failed to create client socket")?;

        let write_count =
            stream.write(PAYLOAD.as_bytes()).await.context("write to tcp client stream failed")?;

        assert_eq!(write_count, PAYLOAD.as_bytes().len());

        let mut buf = [0u8; 1024];
        let read_count =
            stream.read(&mut buf).await.context("read from tcp client stream failed")?;

        assert_eq!(read_count, PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..read_count], PAYLOAD.as_bytes());

        Result::Ok(())
    };

    let ((), ()) = futures::future::try_join(client_fut, server_fut).await?;
    Ok(())
}

#[variants_test]
async fn test_tcp_socket<E: netemul::Endpoint>(name: &str) -> Result {
    const CLIENT_SUBNET: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.2/24);
    const SERVER_SUBNET: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.1/24);

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let net = sandbox.create_network("net").await.context("failed to create network")?;

    let client = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_client", name))
        .context("failed to create client environment")?;
    let _client_ep = client
        .join_network::<E, _>(&net, "client", netemul::InterfaceConfig::StaticIp(CLIENT_SUBNET))
        .await
        .context("client failed to join network")?;

    let server = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_server", name))
        .context("failed to create server environment")?;
    let _server_ep = server
        .join_network::<E, _>(&net, "server", netemul::InterfaceConfig::StaticIp(SERVER_SUBNET))
        .await
        .context("server failed to join network")?;

    run_tcp_socket_test(&server, SERVER_SUBNET.addr, &client, CLIENT_SUBNET.addr).await
}

// Helper function to add ip device to stack.
async fn install_ip_device(
    env: &netemul::TestEnvironment<'_>,
    device: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_network::DeviceMarker>,
    addrs: &mut [fidl_fuchsia_net::Subnet],
) -> Result<u64> {
    let stack = env.connect_to_service::<fidl_fuchsia_net_stack::StackMarker>()?;
    let interface_state = env.connect_to_service::<fidl_fuchsia_net_interfaces::StateMarker>()?;
    let id = stack
        .add_interface(
            fidl_fuchsia_net_stack::InterfaceConfig { name: None, topopath: None, metric: None },
            &mut fidl_fuchsia_net_stack::DeviceDefinition::Ip(device),
        )
        .await
        .squash_result()
        .context("failed to add to stack")?;
    let () =
        stack.enable_interface(id).await.squash_result().context("failed to enable interface")?;
    for addr in addrs.iter_mut() {
        let () = stack
            .add_interface_address(id, addr)
            .await
            .squash_result()
            .with_context(|| format!("failed to add interface address {:?}", addr))?;
    }
    // Wait for addresses to be assigned. Necessary for IPv6 addresses
    // since DAD must be performed.
    let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)?,
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(id),
        |properties| {
            let got = properties.addresses.as_ref()?;
            if addrs.iter().all(|want| got.iter().any(|got| got.addr == Some(*want))) {
                Some(())
            } else {
                None
            }
        },
    )
    .await
    .context("failed to observe addresses")?;
    Ok(id)
}

/// Creates default base config for an IP tun device.
fn base_ip_device_config() -> fidl_fuchsia_net_tun::BaseConfig {
    fidl_fuchsia_net_tun::BaseConfig {
        mtu: Some(1500),
        rx_types: Some(vec![
            fidl_fuchsia_hardware_network::FrameType::Ipv4,
            fidl_fuchsia_hardware_network::FrameType::Ipv6,
        ]),
        tx_types: Some(vec![
            fidl_fuchsia_hardware_network::FrameTypeSupport {
                type_: fidl_fuchsia_hardware_network::FrameType::Ipv4,
                features: fidl_fuchsia_hardware_network::FRAME_FEATURES_RAW,
                supported_flags: fidl_fuchsia_hardware_network::TxFlags::empty(),
            },
            fidl_fuchsia_hardware_network::FrameTypeSupport {
                type_: fidl_fuchsia_hardware_network::FrameType::Ipv6,
                features: fidl_fuchsia_hardware_network::FRAME_FEATURES_RAW,
                supported_flags: fidl_fuchsia_hardware_network::TxFlags::empty(),
            },
        ]),
        report_metadata: None,
        min_tx_buffer_length: None,
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_ip_endpoints_socket() -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let client = sandbox
        .create_netstack_environment::<Netstack2, _>("test_ip_endpoints_socket_client")
        .context("failed to create client environment")?;
    let server = sandbox
        .create_netstack_environment::<Netstack2, _>("test_ip_endpoints_socket_server")
        .context("failed to create server environment")?;

    let tun = client
        .connect_to_service::<fidl_fuchsia_net_tun::ControlMarker>()
        .context("failed to connect to tun service")?;

    let (tun_pair, req) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::DevicePairMarker>()?;
    let () = tun
        .create_pair(
            fidl_fuchsia_net_tun::DevicePairConfig {
                base: Some(base_ip_device_config()),
                fallible_transmit_left: None,
                fallible_transmit_right: None,
                mac_left: None,
                mac_right: None,
            },
            req,
        )
        .context("failed to create tun pair")?;

    let (client_device, client_req) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_hardware_network::DeviceMarker>()?;
    let (server_device, server_req) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_hardware_network::DeviceMarker>()?;
    let () = tun_pair
        .connect_protocols(fidl_fuchsia_net_tun::DevicePairEnds {
            left: Some(fidl_fuchsia_net_tun::Protocols {
                network_device: Some(client_req),
                mac_addressing: None,
            }),
            right: Some(fidl_fuchsia_net_tun::Protocols {
                network_device: Some(server_req),
                mac_addressing: None,
            }),
        })
        .context("connect protocols failed")?;

    // Addresses must be in the same subnet.
    const SERVER_ADDR_V4: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.1/24);
    const SERVER_ADDR_V6: fidl_fuchsia_net::Subnet = fidl_subnet!(2001::1/120);
    const CLIENT_ADDR_V4: fidl_fuchsia_net::Subnet = fidl_subnet!(192.168.0.2/24);
    const CLIENT_ADDR_V6: fidl_fuchsia_net::Subnet = fidl_subnet!(2001::2/120);

    // We install both devices in parallel because a DevicePair will only have
    // its link signal set to up once both sides have sessions attached. This
    // way both devices will be configured "at the same time" and DAD will be
    // able to complete for IPv6 addresses.
    let (_client_id, _server_id) = futures::future::try_join(
        install_ip_device(&client, client_device, &mut [CLIENT_ADDR_V4, CLIENT_ADDR_V6])
            .map(|r| r.context("client setup failed")),
        install_ip_device(&server, server_device, &mut [SERVER_ADDR_V4, SERVER_ADDR_V6])
            .map(|r| r.context("server setup failed")),
    )
    .await
    .context("setup failed")?;

    // Run socket test for both IPv4 and IPv6.
    let () = run_udp_socket_test(&server, SERVER_ADDR_V4.addr, &client, CLIENT_ADDR_V4.addr)
        .await
        .context("v4 socket test failed")?;
    let () = run_udp_socket_test(&server, SERVER_ADDR_V6.addr, &client, CLIENT_ADDR_V6.addr)
        .await
        .context("v6 socket test failed")?;

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_ip_endpoint_packets() -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>("test_ip_endpoint_packets")
        .context("failed to create client environment")?;

    let tun = env
        .connect_to_service::<fidl_fuchsia_net_tun::ControlMarker>()
        .context("failed to connect to tun service")?;

    let (tun_dev, req) = fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::DeviceMarker>()?;
    let () = tun
        .create_device(
            fidl_fuchsia_net_tun::DeviceConfig {
                base: Some(base_ip_device_config()),
                online: Some(true),
                blocking: Some(true),
                mac: None,
            },
            req,
        )
        .context("failed to create tun pair")?;

    let (device, device_req) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_hardware_network::DeviceMarker>()?;
    let () = tun_dev
        .connect_protocols(fidl_fuchsia_net_tun::Protocols {
            network_device: Some(device_req),
            mac_addressing: None,
        })
        .context("connect protocols failed")?;

    // Declare addresses in the same subnet. Alice is Netstack, and Bob is our
    // end of the tun device that we'll use to inject frames.
    const PREFIX_V4: u8 = 24;
    const PREFIX_V6: u8 = 120;
    const ALICE_ADDR_V4: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!(192.168.0.1);
    const ALICE_ADDR_V6: fidl_fuchsia_net::Ipv6Address = fidl_ip_v6!(2001::1);
    const BOB_ADDR_V4: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!(192.168.0.2);
    const BOB_ADDR_V6: fidl_fuchsia_net::Ipv6Address = fidl_ip_v6!(2001::2);

    let _device_id = install_ip_device(
        &env,
        device,
        &mut [
            fidl_fuchsia_net::Subnet {
                addr: fidl_fuchsia_net::IpAddress::Ipv4(ALICE_ADDR_V4),
                prefix_len: PREFIX_V4,
            },
            fidl_fuchsia_net::Subnet {
                addr: fidl_fuchsia_net::IpAddress::Ipv6(ALICE_ADDR_V6),
                prefix_len: PREFIX_V6,
            },
        ],
    )
    .await
    .context("setup failed")?;

    use net_types::ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
    use packet::ParsablePacket;
    use packet_formats::{
        icmp::{
            IcmpEchoRequest, IcmpPacketBuilder, IcmpUnusedCode, Icmpv4Packet, Icmpv6Packet,
            MessageBody,
        },
        ipv4::{Ipv4Packet, Ipv4PacketBuilder},
        ipv6::{Ipv6Packet, Ipv6PacketBuilder},
    };

    let read_frame = futures::stream::try_unfold(tun_dev.clone(), |tun_dev| async move {
        let frame = tun_dev
            .read_frame()
            .await
            .context("read_frame_failed")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("read_frame returned error")?;
        Ok(Some((frame, tun_dev)))
    })
    .try_filter_map(|frame| async move {
        let frame_type = frame.frame_type.context("missing frame type in frame")?;
        let frame_data = frame.data.context("missing data in frame")?;
        if frame_type == fidl_fuchsia_hardware_network::FrameType::Ipv6 {
            // Ignore all NDP IPv6 frames.
            let mut bv = &frame_data[..];
            let ipv6 = Ipv6Packet::parse(&mut bv, ())
                .with_context(|| format!("failed to parse IPv6 packet {:?}", frame_data))?;
            if ipv6.proto() == packet_formats::ip::IpProto::Icmpv6 {
                let parse_args =
                    packet_formats::icmp::IcmpParseArgs::new(ipv6.src_ip(), ipv6.dst_ip());
                if let Icmpv6Packet::Ndp(p) = Icmpv6Packet::parse(&mut bv, parse_args)
                    .context("failed to parse ICMP packet")?
                {
                    println!("ignoring NDP packet {:?}", p);
                    return Ok(None);
                }
            }
        }
        Ok(Some((frame_type, frame_data)))
    });
    pin_utils::pin_mut!(read_frame);

    async fn write_frame_and_read_with_timeout<S>(
        tun_dev: &fidl_fuchsia_net_tun::DeviceProxy,
        frame: fidl_fuchsia_net_tun::Frame,
        read_frame: &mut S,
    ) -> Result<Option<S::Ok>>
    where
        S: futures::stream::TryStream<Error = anyhow::Error> + std::marker::Unpin,
    {
        let () = tun_dev
            .write_frame(frame)
            .await
            .context("write_frame failed")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("write_frame returned error")?;
        Ok(read_frame
            .try_next()
            .and_then(|f| {
                futures::future::ready(f.context("frame stream ended unexpectedly").map(Some))
            })
            .on_timeout(
                fuchsia_async::Time::after(fuchsia_zircon::Duration::from_millis(50)),
                || Ok(None),
            )
            .await
            .context("failed to read frame")?)
    }

    const ICMP_ID: u16 = 10;
    const SEQ_NUM: u16 = 1;
    let mut payload = [1u8, 2, 3, 4];

    // Manually build a ping frame and see it come back out of the stack.
    let src_ip = Ipv4Addr::new(BOB_ADDR_V4.addr);
    let dst_ip = Ipv4Addr::new(ALICE_ADDR_V4.addr);
    let packet = packet::Buf::new(&mut payload[..], ..)
        .encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
            src_ip,
            dst_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(ICMP_ID, SEQ_NUM),
        ))
        .encapsulate(Ipv4PacketBuilder::new(src_ip, dst_ip, 1, packet_formats::ip::IpProto::Icmp))
        .serialize_vec_outer()
        .expect("serialization failed")
        .as_ref()
        .to_vec();

    // Send v4 ping request.
    let () = tun_dev
        .write_frame(fidl_fuchsia_net_tun::Frame {
            frame_type: Some(fidl_fuchsia_hardware_network::FrameType::Ipv4),
            data: Some(packet.clone()),
            meta: None,
        })
        .await
        .context("write_frame failed")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("write_frame returned error")?;

    // Read ping response.
    let (frame_type, data) = read_frame
        .try_next()
        .await
        .context("failed to read ping response")?
        .context("frame stream ended unexpectedly")?;
    assert_eq!(frame_type, fidl_fuchsia_hardware_network::FrameType::Ipv4);
    let mut bv = &data[..];
    let ipv4_packet = Ipv4Packet::parse(&mut bv, ()).context("failed to parse IPv4 packet")?;
    assert_eq!(ipv4_packet.src_ip(), dst_ip);
    assert_eq!(ipv4_packet.dst_ip(), src_ip);
    assert_eq!(ipv4_packet.proto(), packet_formats::ip::IpProto::Icmp);

    let parse_args =
        packet_formats::icmp::IcmpParseArgs::new(ipv4_packet.src_ip(), ipv4_packet.dst_ip());
    let icmp_packet =
        match Icmpv4Packet::parse(&mut bv, parse_args).context("failed to parse ICMP packet")? {
            Icmpv4Packet::EchoReply(reply) => reply,
            p => return Err(anyhow::anyhow!("got ICMP packet {:?}, want EchoReply", p)),
        };
    assert_eq!(icmp_packet.message().id(), ICMP_ID);
    assert_eq!(icmp_packet.message().seq(), SEQ_NUM);
    assert_eq!(icmp_packet.body().bytes(), &payload[..]);

    // Send the same data again, but with an IPv6 frame type, expect that it'll
    // fail parsing and no response will be generated.
    assert_eq!(
        write_frame_and_read_with_timeout(
            &tun_dev,
            fidl_fuchsia_net_tun::Frame {
                frame_type: Some(fidl_fuchsia_hardware_network::FrameType::Ipv6),
                data: Some(packet),
                meta: None,
            },
            &mut read_frame,
        )
        .await
        .context("IPv4 frame with IPv6 frame type failure")?,
        None
    );

    // Manually build a V6 ping frame and see it come back out of the stack.
    let src_ip = Ipv6Addr::new(BOB_ADDR_V6.addr);
    let dst_ip = Ipv6Addr::new(ALICE_ADDR_V6.addr);
    let packet = packet::Buf::new(&mut payload[..], ..)
        .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
            src_ip,
            dst_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(ICMP_ID, SEQ_NUM),
        ))
        .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, 1, packet_formats::ip::IpProto::Icmpv6))
        .serialize_vec_outer()
        .expect("serialization failed")
        .as_ref()
        .to_vec();

    // Send v6 ping request.
    let () = tun_dev
        .write_frame(fidl_fuchsia_net_tun::Frame {
            frame_type: Some(fidl_fuchsia_hardware_network::FrameType::Ipv6),
            data: Some(packet.clone()),
            meta: None,
        })
        .await
        .context("write_frame failed")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("write_frame returned error")?;

    // Read ping response.
    let (frame_type, data) = read_frame
        .try_next()
        .await
        .context("failed to read ping response")?
        .context("frame stream ended unexpectedly")?;
    assert_eq!(frame_type, fidl_fuchsia_hardware_network::FrameType::Ipv6);
    let mut bv = &data[..];
    let ipv6_packet = Ipv6Packet::parse(&mut bv, ()).context("failed to parse IPv6 packet")?;
    assert_eq!(ipv6_packet.src_ip(), dst_ip);
    assert_eq!(ipv6_packet.dst_ip(), src_ip);
    assert_eq!(ipv6_packet.proto(), packet_formats::ip::IpProto::Icmpv6);

    let parse_args =
        packet_formats::icmp::IcmpParseArgs::new(ipv6_packet.src_ip(), ipv6_packet.dst_ip());
    let icmp_packet =
        match Icmpv6Packet::parse(&mut bv, parse_args).context("failed to parse ICMPv6 packet")? {
            Icmpv6Packet::EchoReply(reply) => reply,
            p => return Err(anyhow::anyhow!("got ICMPv6 packet {:?}, want EchoReply", p)),
        };
    assert_eq!(icmp_packet.message().id(), ICMP_ID);
    assert_eq!(icmp_packet.message().seq(), SEQ_NUM);
    assert_eq!(icmp_packet.body().bytes(), &payload[..]);

    // Send the same data again, but with an IPv4 frame type, expect that it'll
    // fail parsing and no response will be generated.
    assert_eq!(
        write_frame_and_read_with_timeout(
            &tun_dev,
            fidl_fuchsia_net_tun::Frame {
                frame_type: Some(fidl_fuchsia_hardware_network::FrameType::Ipv4),
                data: Some(packet),
                meta: None,
            },
            &mut read_frame,
        )
        .await
        .context("IPv6 frame with IPv4 frame type failure")?,
        None
    );
    Ok(())
}
