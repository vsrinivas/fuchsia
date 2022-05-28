// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fdio::{SpawnAction, SpawnOptions};
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_netstack as fnetstack;
use fidl_fuchsia_posix_socket as fposix_socket;
use fidl_fuchsia_posix_socket_packet as fposix_socket_packet;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_runtime::{duplicate_utc_clock_handle, job_default, HandleInfo, HandleType};
use fuchsia_zircon::{self as zx, HandleBased as _, ProcessInfo};
use futures::{
    future,
    io::{AsyncBufReadExt, AsyncReadExt, BufReader},
    stream::StreamExt as _,
};
use libc::{STDERR_FILENO, STDOUT_FILENO};
use net_declare::{fidl_ip_v4, fidl_mac, std_socket_addr};
use netemul::RealmUdpSocket as _;
use netstack_testing_common::{
    interfaces,
    realms::{Netstack2, TestSandboxExt as _},
};
use netstack_testing_macros::variants_test;
use packet::{Buf, Serializer};
use packet_formats::{
    ethernet::{EtherType, EthernetFrameBuilder},
    ip::{IpProto, Ipv4Proto},
    ipv4::Ipv4PacketBuilder,
    udp::UdpPacketBuilder,
};
use regex::Regex;
use std::{
    convert::{TryFrom as _, TryInto as _},
    ffi::{CStr, CString},
    num::NonZeroU16,
};

const BINARY_PATH: &str = "/pkg/bin/tcpdump";

/// Returns true iff the patterns were found, false if the stream ended.
async fn wait_for_pattern<SO: AsyncBufReadExt + Unpin, SE: AsyncReadExt + Unpin>(
    reader: &mut SO,
    other_reader: &mut SE,
    mut patterns: Vec<Regex>,
) {
    while !patterns.is_empty() {
        let mut line = String::new();
        let read_bytes = reader.read_line(&mut line).await.expect("read_line");

        if read_bytes == 0 {
            let mut buf = String::new();
            let _read_bytes: usize =
                other_reader.read_to_string(&mut buf).await.expect("read other reader");
            panic!(
                "failed to match all patterns from reader; patterns = {:?}\nOTHER READER:\n{}",
                patterns, buf
            )
        }

        println!("GOT LINE FROM READER: {}", line);

        // Trim the trailing new line.
        let line = &line[..line.len() - 1];
        let () = patterns.retain(|pattern| !pattern.is_match(&line));
    }
}

fn start_tcpdump(
    args: impl IntoIterator<Item = &'static str>,
    mut spawn_actions: Vec<SpawnAction<'_>>,
) -> (zx::Process, zx::Socket, zx::Socket) {
    let (stdout_reader, stdout_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stdout socket");
    let (stderr_reader, stderr_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stderr socket");

    // The reader-ends should not write.
    let () = stdout_writer.half_close().expect("stdout_reader.half_close");
    let () = stdout_writer.half_close().expect("stderr_reader.half_close");

    let path = CString::new(BINARY_PATH).expect("cstring path");
    let path = path.as_c_str();

    let args: Vec<CString> = std::iter::once(BINARY_PATH)
        .chain(args.into_iter())
        .map(|a| {
            CString::new(a).unwrap_or_else(|e| panic!("failed to parse {} to CString: {}", a, e))
        })
        .collect();
    let args: Vec<&CStr> = args.iter().map(|s| s.as_c_str()).collect();

    // Provide the socket that TCPDump should use for stdout.
    spawn_actions.push(SpawnAction::add_handle(
        HandleInfo::new(
            HandleType::FileDescriptor,
            STDOUT_FILENO.try_into().expect("STDOUT_FILENO.try_into"),
        ),
        stdout_writer.into(),
    ));
    // Provide the socket that TCPDump should use for stderr.
    spawn_actions.push(SpawnAction::add_handle(
        HandleInfo::new(
            HandleType::FileDescriptor,
            STDERR_FILENO.try_into().expect("STDERR_FILENO.try_into"),
        ),
        stderr_writer.into(),
    ));

    let process = fdio::spawn_etc(
        &job_default(),
        SpawnOptions::DEFAULT_LOADER,
        path,
        &args[..],
        None,
        &mut spawn_actions,
    )
    .expect("spawn tcpdump");

    (process, stdout_reader, stderr_reader)
}

enum SendToAddress {
    BoundAddress,
    Specified(std::net::SocketAddr),
}

async fn start_tcpdump_and_wait_for_patterns<
    Fut: future::Future<Output = ()>,
    F: FnOnce() -> Fut,
>(
    realm: &netemul::TestRealm<'_>,
    args: impl IntoIterator<Item = &'static str>,
    bind_addr: std::net::SocketAddr,
    send_to_addr: SendToAddress,
    inject_packet: F,
    patterns: Vec<Regex>,
) {
    let (svc_client_end, svc_server_end) = zx::Channel::create().expect("create channel");
    let (process, stdout_reader, stderr_reader) = start_tcpdump(
        args,
        vec![
            SpawnAction::add_namespace_entry(
                CString::new("/svc").expect("CString /svc").as_c_str(),
                svc_client_end.into_handle(),
            ),
            SpawnAction::add_handle(
                HandleInfo::new(HandleType::ClockUtc, 0),
                duplicate_utc_clock_handle(
                    zx::Rights::READ | zx::Rights::WAIT | zx::Rights::TRANSFER,
                )
                .expect("duplicate utc clock handle")
                .into_handle(),
            ),
        ],
    );
    let mut stdout_reader =
        BufReader::new(fasync::Socket::from_socket(stdout_reader).expect("async socket stdout"));
    let mut stderr_reader =
        BufReader::new(fasync::Socket::from_socket(stderr_reader).expect("async socket stdout"));

    let mut svcfs = ServiceFs::new_local();
    let svcfs = svcfs
        .add_service_connector::<_, fposix_socket::ProviderMarker>(|server_end| {
            realm
                .connect_to_protocol_with_server_end(server_end)
                .expect("connect to regular socket provider")
        })
        .add_service_connector::<_, fposix_socket_packet::ProviderMarker>(|server_end| {
            realm
                .connect_to_protocol_with_server_end(server_end)
                .expect("connect to packet socket provider")
        })
        .serve_connection(svc_server_end)
        .expect("servicefs serve connection")
        .collect::<()>();

    // Wait for TCPDump to start.
    let svcfs = {
        let wait_for_pattern_fut = wait_for_pattern(
            &mut stderr_reader,
            &mut stdout_reader,
            vec![Regex::new(r"listening on any, link-type LINUX_SLL2 \(Linux cooked v2\), snapshot length \d+ bytes").expect("parse tcpdump listening regex")],
        );
        futures::pin_mut!(wait_for_pattern_fut);
        match future::select(wait_for_pattern_fut, svcfs).await {
            future::Either::Left(((), svcfs)) => svcfs,
            future::Either::Right(((), _wait_for_pattern_fut)) => {
                panic!("service directory unexpectedly ended")
            }
        }
    };

    // Send a UDP packet and make sure TCPDump logs it.
    let sock = fuchsia_async::net::UdpSocket::bind_in_realm(&realm, bind_addr)
        .await
        .expect("create socket");
    let addr = match send_to_addr {
        SendToAddress::Specified(addr) => addr,
        SendToAddress::BoundAddress => sock.local_addr().expect("get bound socket address"),
    };
    const PAYLOAD: [u8; 4] = [1, 2, 3, 4];
    let sent = sock.send_to(&PAYLOAD[..], addr).await.expect("send_to failed");
    assert_eq!(sent, PAYLOAD.len());

    inject_packet().await;

    {
        let wait_for_pattern_fut =
            wait_for_pattern(&mut stdout_reader, &mut stderr_reader, patterns);
        futures::pin_mut!(wait_for_pattern_fut);
        match future::select(wait_for_pattern_fut, svcfs).await {
            future::Either::Left(((), _svcfs)) => {}
            future::Either::Right(((), _wait_for_pattern_fut)) => {
                panic!("service directory unexpectedly ended")
            }
        }
    }

    assert_eq!(
        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .expect("wait for process termination"),
        zx::Signals::PROCESS_TERMINATED
    );
    let ProcessInfo { return_code, start_time: _, flags: _ } =
        process.info().expect("process info");
    assert_eq!(return_code, 0);
}

#[fuchsia::test]
async fn version_test() {
    let (process, stdout_reader, stderr_reader) = start_tcpdump(["--version"], Vec::new());

    assert_eq!(
        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .expect("wait for process termination"),
        zx::Signals::PROCESS_TERMINATED
    );
    let ProcessInfo { return_code, start_time: _, flags: _ } =
        process.info().expect("process info");
    assert_eq!(return_code, 0);

    let mut stdout_reader =
        BufReader::new(fasync::Socket::from_socket(stdout_reader).expect("async socket stdout"));
    let mut stderr_reader =
        fasync::Socket::from_socket(stderr_reader).expect("async socket stderr");

    wait_for_pattern(
        &mut stdout_reader,
        &mut stderr_reader,
        vec![
            Regex::new(r"tcpdump version 4\.99\.1").expect("parse tcpdump version regex"),
            Regex::new(r"libpcap version 1\.10\.1").expect("parse libpcap version regex"),
        ],
    )
    .await
}

#[fuchsia::test]
// TODO(https://fxbug.dev/88133): Fix memory leak and run this with Lsan.
#[cfg_attr(feature = "variant_asan", ignore)]
async fn packet_test() {
    let name = "packet_test";
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");

    start_tcpdump_and_wait_for_patterns(
        &realm,
        ["-c", "1", "--no-promiscuous-mode"],
        std_socket_addr!("127.0.0.1:9875"),
        SendToAddress::BoundAddress,
        || futures::future::ready(()),
        vec![Regex::new(r"lo\s+In\s+IP 127\.0\.0\.1\.9875 > 127\.0\.0\.1\.9875: UDP, length 4")
            .expect("parse tcpdump packet regex")],
    )
    .await
}

#[variants_test]
// TODO(https://fxbug.dev/88133): Fix memory leak and run this with Lsan.
#[cfg_attr(feature = "variant_asan", ignore)]
async fn bridged_packet_test<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");

    let net = sandbox.create_network(name).await.expect("error creating network");
    let iface = realm
        .join_network::<E, _>(&net, "ep")
        .await
        .expect("failed to join network in gateway realm");

    const REMOTE_MAC: fnet::MacAddress = fidl_mac!("02:00:00:00:00:01");
    const NETWORK_ADDR: fnet::Ipv4Address = fidl_ip_v4!("192.168.1.0");
    const PREFIX_LEN: u8 = 24;
    let increment_and_get_addr = |mut new_addr| {
        let fnet::Ipv4Address { addr } = &mut new_addr;
        *addr.last_mut().expect("should have at least 1 byte") += 1;
        new_addr
    };
    let local_addr = increment_and_get_addr(NETWORK_ADDR);
    let remote_addr = increment_and_get_addr(local_addr);
    let local_mac = {
        let netstack = realm
            .connect_to_protocol::<fnetstack::NetstackMarker>()
            .expect("failed to connect to netstack in switch realm");
        let bridge_id = match netstack
            .bridge_interfaces(
                &[u32::try_from(iface.id()).expect("interface ID should fit in u32")][..],
            )
            .await
            .expect("create bridge")
        {
            fnetstack::Result_::Message(message) => panic!("{}", message),
            fnetstack::Result_::Nicid(id) => id,
        };

        let debug = realm
            .connect_to_protocol::<fnet_debug::InterfacesMarker>()
            .expect("failed to connect to debug interfaces protocol");
        let bridge_interface_control = {
            let (client_end, server_end) =
                fidl::endpoints::create_proxy::<fnet_interfaces_admin::ControlMarker>()
                    .expect("failed to create fuchsia.net.interfaces.admin/Control proxy");
            let () = debug
                .get_admin(bridge_id.into(), server_end)
                .expect("error getting bridge interface control");
            fnet_interfaces_ext::admin::Control::new(client_end)
        };
        let did_enable =
            bridge_interface_control.enable().await.expect("send enable").expect("enable");
        assert!(did_enable);
        let address_state_provider = interfaces::add_address_wait_assigned(
            &bridge_interface_control,
            fnet::Subnet { addr: fnet::IpAddress::Ipv4(local_addr), prefix_len: PREFIX_LEN },
            fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
        )
        .await
        .expect("add IPv4 address to bridge failed");
        let () = address_state_provider
            .detach()
            .expect("failed to detach from bridge interface address state provider");
        let stack = realm
            .connect_to_protocol::<fnet_stack::StackMarker>()
            .expect("failed to connect to stack");
        let () = stack
            .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                subnet: fnet::Subnet {
                    addr: fnet::IpAddress::Ipv4(NETWORK_ADDR),
                    prefix_len: PREFIX_LEN,
                },
                device_id: bridge_id.into(),
                next_hop: None,
                metric: 0,
            })
            .await
            .expect("FIDL error adding subnet route to bridge")
            .expect("error adding subnet route to bridge");

        // Create a static neighbor entry so we skip neighbor resolution.
        realm
            .add_neighbor_entry(bridge_id.into(), fnet::IpAddress::Ipv4(remote_addr), REMOTE_MAC)
            .await
            .expect("error adding neighbor entry");

        debug
            .get_mac(bridge_id.into())
            .await
            .expect("error calling get_mac")
            .expect("error getting bridge's MAC address")
            .expect("expected bridge to have a MAC address")
    };

    start_tcpdump_and_wait_for_patterns(
        &realm,
        ["-i", "any", "-c", "4", "--no-promiscuous-mode", "udp"],
        std_socket_addr!("0.0.0.0:9876"),
        SendToAddress::Specified(std::net::SocketAddr::new(
            std::net::IpAddr::V4(std::net::Ipv4Addr::from(remote_addr.addr)),
            1234,
        )),
        || async {
            let net_types_addr = |fnet::Ipv4Address { addr }| net_types::ip::Ipv4Addr::new(addr);
            let local_addr = net_types_addr(local_addr);
            let remote_addr = net_types_addr(remote_addr);

            let net_types_mac = |fnet::MacAddress { octets }| net_types::ethernet::Mac::new(octets);

            let bytes = Buf::new(&mut [0; 8][..], ..)
                .encapsulate(UdpPacketBuilder::new(
                    remote_addr,
                    local_addr,
                    Some(NonZeroU16::new(2342).expect("non zero value should be valid")),
                    NonZeroU16::new(9876).expect("non zero value should be valid"),
                ))
                .encapsulate(Ipv4PacketBuilder::new(
                    remote_addr,
                    local_addr,
                    64, /* ttl */
                    Ipv4Proto::Proto(IpProto::Udp),
                ))
                .encapsulate(EthernetFrameBuilder::new(
                    net_types_mac(REMOTE_MAC),
                    net_types_mac(*local_mac),
                    EtherType::Ipv4,
                ))
                .serialize_vec_outer()
                .expect("error serializing UDP packet")
                .unwrap_b();

            let fake_ep = net.create_fake_endpoint().expect("error creating fake endppint");
            fake_ep.write(bytes.as_ref()).await.expect("error writing packet to fake endpoint")
        },
        vec![
            Regex::new(
                r"br\d+\s+Out\s+IP 192\.168\.1\.1\.9876 > 192\.168\.1\.2\.1234: UDP, length 4",
            )
            .expect("parse tcpdump packet regex for packet sent through bridge"),
            Regex::new(
                r"eth\d+\s+Out\s+IP 192\.168\.1\.1\.9876 > 192\.168\.1\.2\.1234: UDP, length 4",
            )
            .expect("parse tcpdump packet regex for packet sent through ethernet interface"),
            Regex::new(
                r"eth\d+\s+In\s+IP 192\.168\.1\.2\.2342 > 192\.168\.1\.1\.9876: UDP, length 8",
            )
            .expect("parse tcpdump packet regex for packet received at ethernet interface"),
            Regex::new(
                r"br\d+\s+In\s+IP 192\.168\.1\.2\.2342 > 192\.168\.1\.1\.9876: UDP, length 8",
            )
            .expect("parse tcpdump packet regex for packet received at bridge"),
        ],
    )
    .await
}
